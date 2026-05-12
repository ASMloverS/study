#include "ms/serializer.h"
#include "ms/fs_util.h"
#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/chunk.h"
#include "ms/module.h"
#include "ms/object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>   /* GetCurrentProcessId */
#else
#  include <unistd.h>    /* getpid */
#endif

_Static_assert(sizeof(MsMscHeader) == 32, "MsMscHeader must be 32 bytes");

#define TAG_NIL  0
#define TAG_BOOL 1
#define TAG_INT  2
#define TAG_NUM  3
#define TAG_STR  4
#define TAG_FN   5

#define W1(f,v)  do { uint8_t  _=(uint8_t)(v);  fwrite(&_,1,1,f); } while(0)
#define W4(f,v)  do { uint32_t _=(uint32_t)(v); fwrite(&_,4,1,f); } while(0)
#define W4S(f,v) do { int32_t  _=(int32_t)(v);  fwrite(&_,4,1,f); } while(0)
#define W8I(f,v) do { int64_t  _=(int64_t)(v);  fwrite(&_,8,1,f); } while(0)
#define W8F(f,v) do { double   _=(double)(v);   fwrite(&_,8,1,f); } while(0)

#define R1(f,v)  do { uint8_t  _=0; if(fread(&_,1,1,f)!=(size_t)1) goto fail; (v)=_; } while(0)
#define R4(f,v)  do { uint32_t _=0; if(fread(&_,4,1,f)!=(size_t)1) goto fail; (v)=_; } while(0)
#define R4S(f,v) do { int32_t  _=0; if(fread(&_,4,1,f)!=(size_t)1) goto fail; (v)=_; } while(0)
#define R8I(f,v) do { int64_t  _=0; if(fread(&_,8,1,f)!=(size_t)1) goto fail; (v)=_; } while(0)
#define R8F(f,v) do { double   _=0.0; if(fread(&_,8,1,f)!=(size_t)1) goto fail; (v)=_; } while(0)

#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

typedef struct { MsObjFunction** data; int count; int cap; } FnArray;

static void fn_push(FnArray* a, MsObjFunction* fn) {
    if (a->count >= a->cap) {
        a->cap = a->cap < 8 ? 8 : a->cap * 2;
        a->data = (MsObjFunction**)realloc(a->data,
                  sizeof(MsObjFunction*) * (size_t)a->cap);
        if (!a->data) abort();
    }
    a->data[a->count++] = fn;
}

static void collect_fns(MsObjFunction* fn, FnArray* arr) {
    for (int i = 0; i < fn->chunk.constants.count; i++) {
        MsValue v = fn->chunk.constants.data[i];
        if (MS_IS_OBJ_TYPE(v, MS_OBJ_FUNCTION))
            collect_fns(MS_AS_FUNCTION(v), arr);
    }
    fn_push(arr, fn);
}

static void write_constant(FILE* f, MsValue v, FnArray* fns) {
    if (MS_IS_NIL(v)) {
        W1(f, TAG_NIL);
    } else if (MS_IS_BOOL(v)) {
        W1(f, TAG_BOOL); W1(f, MS_AS_BOOL(v) ? 1 : 0);
    } else if (MS_IS_INT(v)) {
        W1(f, TAG_INT); W8I(f, MS_AS_INT(v));
    } else if (MS_IS_NUMBER(v)) {
        W1(f, TAG_NUM); W8F(f, MS_AS_NUMBER(v));
    } else if (MS_IS_STRING(v)) {
        MsObjString* s = MS_AS_STRING(v);
        W1(f, TAG_STR); W4(f, s->length);
        fwrite(s->data, 1, (size_t)s->length, f);
    } else if (MS_IS_OBJ_TYPE(v, MS_OBJ_FUNCTION)) {
        MsObjFunction* fn = MS_AS_FUNCTION(v);
        uint32_t idx = 0;
        for (int j = 0; j < fns->count; j++)
            if (fns->data[j] == fn) { idx = (uint32_t)j; break; }
        W1(f, TAG_FN); W4(f, idx);
    }
}

static void write_fn(FILE* f, MsObjFunction* fn, FnArray* fns) {
    uint32_t nl = fn->name ? (uint32_t)fn->name->length : 0;
    W4(f, nl);
    if (nl) fwrite(fn->name->data, 1, nl, f);
    W4(f, fn->arity); W4S(f, fn->min_arity);
    W4(f, fn->upvalue_count); W4(f, fn->max_stack_size);
    W1(f, fn->is_generator ? 1 : 0);
    W4(f, (uint32_t)fn->ic_count);
    W4(f, fn->chunk.code_count);
    fwrite(fn->chunk.code, sizeof(MsInstruction),
           (size_t)fn->chunk.code_count, f);
    W4(f, fn->chunk.constants.count);
    for (int i = 0; i < fn->chunk.constants.count; i++)
        write_constant(f, fn->chunk.constants.data[i], fns);
    W4(f, fn->chunk.line_count);
    for (int i = 0; i < fn->chunk.line_count; i++) {
        MsSourceRun r = fn->chunk.lines[i];
        W4(f, r.line); W4(f, r.column); W4(f, r.count);
    }
}

static MsObjFunction* read_fn(FILE* f, MsVM* vm,
                               MsObjFunction** fns, int fn_idx) {
    MsObjFunction* fn = ms_obj_function_new(vm);
    char* buf = NULL;
    uint32_t tmp4 = 0; int32_t tmp4s = 0; uint8_t tmp1 = 0;
    int64_t tmp8i = 0; double tmp8f = 0.0;
    R4(f, tmp4);
    if (tmp4 > 0) {
        buf = (char*)malloc(tmp4 + 1);
        if (!buf) goto fail;
        if (fread(buf, 1, tmp4, f) != (size_t)tmp4) goto fail;
        buf[tmp4] = '\0';
        fn->name = ms_obj_string_copy(vm, buf, (int)tmp4);
        free(buf); buf = NULL;
    }
    R4(f, tmp4); fn->arity = (int)tmp4;
    R4S(f, tmp4s); fn->min_arity = tmp4s;
    R4(f, tmp4); fn->upvalue_count = (int)tmp4;
    R4(f, tmp4); fn->max_stack_size = (int)tmp4;
    R1(f, tmp1); fn->is_generator = tmp1 != 0;
    R4(f, tmp4); fn->ic_count = (int)tmp4;
    R4(f, tmp4);
    if (tmp4 > 0) {
        fn->chunk.code = (MsInstruction*)malloc(sizeof(MsInstruction) * tmp4);
        if (!fn->chunk.code) goto fail;
        if (fread(fn->chunk.code, sizeof(MsInstruction), tmp4, f) != (size_t)tmp4) goto fail;
        fn->chunk.code_count = fn->chunk.code_capacity = (int)tmp4;
    }
    R4(f, tmp4);
    for (uint32_t i = 0; i < tmp4; i++) {
        R1(f, tmp1); MsValue v = MS_NIL_VAL();
        if (tmp1 == TAG_BOOL) { uint8_t b = 0; R1(f,b); v=MS_BOOL_VAL(b!=0); }
        else if (tmp1 == TAG_INT)  { R8I(f, tmp8i); v=MS_INT_VAL(tmp8i); }
        else if (tmp1 == TAG_NUM)  { R8F(f, tmp8f); v=MS_NUMBER_VAL(tmp8f); }
        else if (tmp1 == TAG_STR)  {
            uint32_t sl = 0; R4(f,sl);
            buf = (char*)malloc(sl + 1);
            if (!buf) goto fail;
            if (fread(buf, 1, sl, f) != (size_t)sl) goto fail;
            buf[sl] = '\0';
            v = MS_OBJ_VAL(ms_obj_string_copy(vm, buf, (int)sl));
            free(buf); buf = NULL;
        } else if (tmp1 == TAG_FN) {
            uint32_t idx = 0; R4(f,idx);
            if ((int)idx >= fn_idx) goto fail;
            v = MS_OBJ_VAL(fns[idx]);
        } else if (tmp1 != TAG_NIL) goto fail;
        ms_chunk_add_constant(&fn->chunk, v);
    }
    R4(f, tmp4);
    if (tmp4 > 0) {
        fn->chunk.lines = (MsSourceRun*)malloc(sizeof(MsSourceRun) * tmp4);
        if (!fn->chunk.lines) goto fail;
        fn->chunk.line_count = fn->chunk.line_capacity = (int)tmp4;
        for (uint32_t j = 0; j < tmp4; j++) {
            uint32_t l=0,c=0,ct=0; R4(f,l); R4(f,c); R4(f,ct);
            fn->chunk.lines[j] = (MsSourceRun){(int)l,(int)c,(int)ct};
        }
    }
    return fn;
fail:
    free(buf); return NULL;
}

static MsObjFunction* load_fns_from_file(MsVM* vm, FILE* f) {
    uint32_t fn_count = 0;
    if (fread(&fn_count, 4, 1, f) != (size_t)1 || fn_count == 0) return NULL;
    MsObjFunction** fns = (MsObjFunction**)calloc(fn_count, sizeof(*fns));
    if (!fns) return NULL;
    size_t saved = vm->next_gc; vm->next_gc = (size_t)-1;
    for (uint32_t i = 0; i < fn_count; i++) {
        fns[i] = read_fn(f, vm, fns, (int)i);
        if (!fns[i]) { vm->next_gc = saved; free(fns); return NULL; }
    }
    MsObjFunction* res = fns[fn_count - 1];
    vm->next_gc = saved;
    free(fns);
    return res;
}

/* ---- public API ---- */

bool ms_serialize(MsObjFunction* fn, const char* path, const MsMscHeader* hdr) {
    FnArray fns = {NULL, 0, 0};
    collect_fns(fn, &fns);
    FILE* f = fopen(path, "wb");
    if (!f) { free(fns.data); return false; }
    fwrite(hdr, sizeof(*hdr), 1, f);
    W4(f, fns.count);
    for (int i = 0; i < fns.count; i++)
        write_fn(f, fns.data[i], &fns);
    fclose(f);
    free(fns.data);
    return true;
}

MsObjFunction* ms_deserialize(MsVM* vm, const char* path,
                               uint64_t src_size, int64_t src_mtime_ns,
                               uint32_t src_hash) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    MsMscHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != (size_t)1) goto fail_f;
    if (memcmp(hdr.magic, "MSC", 3) || hdr.magic[3] || hdr.version != MS_MSC_VERSION)
        goto fail_f;
    /* Validate based on cache mode stored in header */
    if (hdr.flags & MS_CACHE_HASH) {
        if (hdr.src_hash != src_hash) goto fail_f;
    } else {
        if (hdr.src_size != src_size || hdr.src_mtime_ns != src_mtime_ns) goto fail_f;
    }
    uint32_t fn_count = 0;
    if (fread(&fn_count, 4, 1, f) != (size_t)1 || fn_count == 0) goto fail_f;
    MsObjFunction** fns = (MsObjFunction**)calloc(fn_count, sizeof(*fns));
    if (!fns) goto fail_f;
    size_t saved = vm->next_gc; vm->next_gc = (size_t)-1;
    for (uint32_t i = 0; i < fn_count; i++) {
        fns[i] = read_fn(f, vm, fns, (int)i);
        if (!fns[i]) { vm->next_gc = saved; free(fns); goto fail_f; }
    }
    MsObjFunction* res = fns[fn_count - 1];
    vm->next_gc = saved; free(fns); fclose(f);
    return res;
fail_f:
    fclose(f); return NULL;
}

/* Return index of last '/' or '\\' in path, or -1 if none. */
static int last_sep(const char* path) {
    int last = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/' || path[i] == '\\') last = i;
    return last;
}

bool ms_cache_path_for(const char* src_path, char* out, size_t cap) {
    int sep = last_sep(src_path);
    const char* base = (sep >= 0) ? src_path + sep + 1 : src_path;
    size_t blen = strlen(base);
    /* Strip ".ms" extension */
    if (blen > 3 &&
        base[blen-3] == '.' && base[blen-2] == 'm' && base[blen-1] == 's')
        blen -= 3;
    int written;
    if (sep >= 0)
        written = snprintf(out, cap, "%.*s__mscache__/%.*s.msc",
                           sep + 1, src_path, (int)blen, base);
    else
        written = snprintf(out, cap, "__mscache__/%.*s.msc", (int)blen, base);
    return written > 0 && (size_t)written < cap;
}

MsObjFunction* ms_compile_cached(MsVM* vm, const char* src_path, uint32_t flags) {
    char cache[PATH_MAX];
    if (!ms_cache_path_for(src_path, cache, sizeof(cache))) return NULL;

    /* Stat source file - needed for mtime validation or existence check */
    MsFileMeta meta;
    bool has_meta = ms_fs_stat(src_path, &meta);

    char* source = NULL;
    uint32_t hash = 0;

    if (flags & MS_CACHE_HASH) {
        /* Hash mode: must read source before checking cache */
        source = ms_read_file(src_path);
        if (!source) return NULL;
        hash = ms_fnv1a(source, (int)strlen(source));
    }

    /* Try cache (mtime mode: source never read on hit) */
    MsObjFunction* fn = ms_deserialize(vm, cache,
                                        has_meta ? meta.size     : 0,
                                        has_meta ? meta.mtime_ns : 0,
                                        hash);
    if (fn) { free(source); return fn; }  /* cache hit; caller sets script_path after rooting fn */

    /* Cache miss: read source if not already read (mtime mode) */
    if (!source) {
        source = ms_read_file(src_path);
        if (!source) return NULL;
        if (!has_meta) has_meta = ms_fs_stat(src_path, &meta);
        hash = 0;
    }

    /* Compile */
    MsDiagnostic diags[32]; int dc = 0;
    fn = ms_compile(vm, source, src_path, diags, &dc, 32);
    if (!fn) {
        for (int i = 0; i < dc; i++)
            fprintf(stderr, "[line %d] %s: %s\n",
                    diags[i].line, src_path, diags[i].message);
        free(source);
        return NULL;
    }

    /* Create __mscache__/ directory (best effort) */
    int sep = last_sep(cache);
    if (sep > 0) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%.*s", sep, cache);
        ms_fs_mkdir(dir);
    }

    /* Build header */
    MsMscHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = 'M'; hdr.magic[1] = 'S'; hdr.magic[2] = 'C'; hdr.magic[3] = '\0';
    hdr.version      = MS_MSC_VERSION;
    hdr.flags        = flags & 1u;
    hdr.src_size     = has_meta ? meta.size     : 0;
    hdr.src_mtime_ns = has_meta ? meta.mtime_ns : 0;
    hdr.src_hash     = hash;

    /* Atomic write: serialize to tmp, then rename */
    char tmp[PATH_MAX];
#ifdef _WIN32
    snprintf(tmp, sizeof(tmp), "%s.tmp.%lu", cache,
             (unsigned long)GetCurrentProcessId());
#else
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", cache, (int)getpid());
#endif

    if (ms_serialize(fn, tmp, &hdr)) {
        if (!ms_fs_atomic_rename(tmp, cache))
            ms_fs_unlink(tmp);   /* cleanup on rename failure */
    }
    /* Any write failure above is silently swallowed */

    free(source);
    return fn;
}

MsObjFunction* ms_load_msc(MsVM* vm, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open '%s'.\n", path); return NULL; }
    MsMscHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != (size_t)1 ||
        memcmp(hdr.magic, "MSC", 3) || hdr.magic[3] ||
        hdr.version != MS_MSC_VERSION) {
        fprintf(stderr, "Not a valid .msc file: %s\n", path);
        fclose(f); return NULL;
    }
    MsObjFunction* res = load_fns_from_file(vm, f);
    fclose(f);
    if (!res) fprintf(stderr, "Failed to load .msc file: %s\n", path);
    return res;
}
