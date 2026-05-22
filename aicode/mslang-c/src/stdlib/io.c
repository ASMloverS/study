#include "ms/stdlib/objfile.h"
#include "ms/stdlib/objbuffer.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/memory.h"
#include "ms/module.h"
#include "ms/threadpool.h"
#include "ms/common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif

/* Cross-platform strerror helper (MSVC deprecates strerror) */
#ifdef _MSC_VER
static const char* io_strerror(int errnum) {
    static char buf[128];
    strerror_s(buf, sizeof(buf), errnum);
    return buf;
}
#else
#  define io_strerror(e) strerror(e)
#endif

/* ---- ObjFile constructor ------------------------------------------ */

MsObjFile* ms_obj_file_new(struct MsVM* vm, FILE* fp, MsFileMode mode, bool owns_fp) {
    MsObjFile* f = MS_ALLOC_OBJ(vm, MS_OBJ_FILE, MsObjFile, 0);
    f->fp        = fp;
    f->mode      = mode;
    f->eof       = false;
    f->owns_fp   = owns_fp;
    f->mode_str[0] = '\0';
    return f;
}

MsObjFile* ms_obj_file_from_fd(struct MsVM* vm, int fd, const char* open_mode) {
#ifdef _WIN32
    FILE* fp = _fdopen(fd, open_mode);
#else
    FILE* fp = fdopen(fd, open_mode);
#endif
    if (!fp) return NULL;
    MsFileMode mode = (strchr(open_mode, 'b') != NULL) ? MS_FILE_BINARY : MS_FILE_TEXT;
    return ms_obj_file_new(vm, fp, mode, true);
}

/* ---- ObjFile method dispatch -------------------------------------- */

bool ms_objfile_invoke(struct MsVM* vm, MsObjFile* f,
                       MsObjString* method, int argc, MsValue* argv,
                       MsValue* out) {
    (void)argc;
    const char* name = method->data;

    if (strcmp(name, "close") == 0) {
        if (f->fp && f->owns_fp) { fclose(f->fp); }
        f->fp = NULL;
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "flush") == 0) {
        if (f->fp) fflush(f->fp);
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "eof") == 0) {
        bool is_eof = f->eof || (f->fp && feof(f->fp));
        *out = MS_BOOL_VAL(is_eof);
        return true;
    }
    if (strcmp(name, "tell") == 0) {
        long pos = f->fp ? ftell(f->fp) : -1;
        *out = MS_INT_VAL((ms_i64)pos);
        return true;
    }
    if (strcmp(name, "fd") == 0) {
        int fd = -1;
        if (f->fp) {
#ifdef _WIN32
            fd = _fileno(f->fp);
#else
            fd = fileno(f->fp);
#endif
        }
        *out = MS_INT_VAL((ms_i64)fd);
        return true;
    }
    if (strcmp(name, "mode") == 0) {
        *out = MS_OBJ_VAL(ms_obj_string_copy(vm, f->mode_str, (int)strlen(f->mode_str)));
        return true;
    }
    if (strcmp(name, "seek") == 0) {
        long new_pos = -1;
        if (f->fp && argc >= 2) {
            long off    = (long)MS_AS_INT(argv[0]);
            int  whence = (int)MS_AS_INT(argv[1]);
            fseek(f->fp, off, whence);
            new_pos = ftell(f->fp);
        } else if (f->fp && argc >= 1) {
            long off = (long)MS_AS_INT(argv[0]);
            fseek(f->fp, off, SEEK_SET);
            new_pos = ftell(f->fp);
        }
        *out = MS_INT_VAL((ms_i64)new_pos);
        return true;
    }
    if (strcmp(name, "write") == 0) {
        size_t written = 0;
        if (f->fp && argc >= 1) {
            if (MS_IS_STRING(argv[0])) {
                MsObjString* s = MS_AS_STRING(argv[0]);
                written = fwrite(s->data, 1, (size_t)s->length, f->fp);
            } else if (MS_IS_BUFFER(argv[0])) {
                MsObjBuffer* b = MS_AS_BUFFER(argv[0]);
                written = fwrite(b->data, 1, b->len, f->fp);
            }
        }
        *out = MS_INT_VAL((ms_i64)written);
        return true;
    }
    if (strcmp(name, "read") == 0) {
        if (!f->fp) { *out = MS_NIL_VAL(); return true; }
        long n = (argc >= 1) ? (long)MS_AS_INT(argv[0]) : -1;
        if (n < 0) {
            /* read to EOF */
            long start = ftell(f->fp);
            fseek(f->fp, 0, SEEK_END);
            long end = ftell(f->fp);
            fseek(f->fp, start, SEEK_SET);
            size_t size = (size_t)(end - start);
            if (size == 0) { f->eof = true; *out = MS_NIL_VAL(); return true; }
            char* buf = (char*)ms_reallocate(vm, NULL, 0, size + 1);
            size_t rd = fread(buf, 1, size, f->fp);
            buf[rd] = '\0';
            { int c = fgetc(f->fp); if (c == EOF) f->eof = true; else ungetc(c, f->fp); }
            if (f->mode == MS_FILE_TEXT) {
                *out = MS_OBJ_VAL(ms_obj_string_take(vm, buf, (int)rd));
            } else {
                MsObjBuffer* b = ms_obj_buffer_from_bytes(vm, (uint8_t*)buf, rd);
                ms_reallocate(vm, buf, rd + 1, 0);
                *out = MS_OBJ_VAL(b);
            }
        } else {
            char* buf = (char*)ms_reallocate(vm, NULL, 0, (size_t)n + 1);
            size_t rd = fread(buf, 1, (size_t)n, f->fp);
            buf[rd] = '\0';
            if (feof(f->fp)) f->eof = true;
            if (rd == 0) { ms_reallocate(vm, buf, (size_t)n + 1, 0); *out = MS_NIL_VAL(); return true; }
            if (f->mode == MS_FILE_TEXT) {
                *out = MS_OBJ_VAL(ms_obj_string_take(vm, buf, (int)rd));
            } else {
                MsObjBuffer* b = ms_obj_buffer_from_bytes(vm, (uint8_t*)buf, rd);
                ms_reallocate(vm, buf, (size_t)n + 1, 0);
                *out = MS_OBJ_VAL(b);
            }
        }
        return true;
    }
    if (strcmp(name, "readline") == 0) {
        if (!f->fp) { *out = MS_NIL_VAL(); return true; }
        char chunk[256];
        char* line = NULL;
        size_t total = 0;
        size_t cap   = 0;
        while (fgets(chunk, sizeof(chunk), f->fp)) {
            size_t len = strlen(chunk);
            if (total + len + 1 > cap) {
                size_t new_cap = cap < 256 ? 256 : cap * 2;
                while (new_cap < total + len + 1) new_cap *= 2;
                line = (char*)ms_reallocate(vm, line, cap, new_cap);
                cap = new_cap;
            }
            memcpy(line + total, chunk, len);
            total += len;
            if (chunk[len - 1] == '\n') break;
        }
        if (total == 0) {
            if (line) ms_reallocate(vm, line, cap, 0);
            if (feof(f->fp)) f->eof = true;
            *out = MS_NIL_VAL();
            return true;
        }
        line[total] = '\0';
        *out = MS_OBJ_VAL(ms_obj_string_take(vm, line, (int)total));
        return true;
    }
    if (strcmp(name, "readlines") == 0) {
        if (!f->fp) { *out = MS_OBJ_VAL(ms_obj_list_new(vm)); return true; }
        MsObjList* list = ms_obj_list_new(vm);
        char chunk[256];
        char* line = NULL;
        size_t total = 0;
        size_t cap   = 0;
        while (fgets(chunk, sizeof(chunk), f->fp)) {
            size_t len = strlen(chunk);
            if (total + len + 1 > cap) {
                size_t new_cap = cap < 256 ? 256 : cap * 2;
                while (new_cap < total + len + 1) new_cap *= 2;
                line = (char*)ms_reallocate(vm, line, cap, new_cap);
                cap = new_cap;
            }
            memcpy(line + total, chunk, len);
            total += len;
            if (chunk[len - 1] == '\n') {
                line[total] = '\0';
                MsValue sv = MS_OBJ_VAL(ms_obj_string_copy(vm, line, (int)total));
                ms_value_array_push(&list->items, sv);
                total = 0;
            }
        }
        /* handle last line without trailing newline */
        if (total > 0) {
            line[total] = '\0';
            MsValue sv = MS_OBJ_VAL(ms_obj_string_copy(vm, line, (int)total));
            ms_value_array_push(&list->items, sv);
        }
        if (line) ms_reallocate(vm, line, cap, 0);
        if (feof(f->fp)) f->eof = true;
        *out = MS_OBJ_VAL(list);
        return true;
    }
    return false;
}

/* ---- Sync IO module functions ------------------------------------ */

static MsValue ms_io_read_file(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "io.read_file: expected string path");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    char* buf = ms_read_file(path);
    if (!buf) {
        ms_vm_runtime_error(vm, "io.read_file: cannot open '%s'", path);
        return MS_NIL_VAL();
    }
    MsValue v = MS_OBJ_VAL(ms_obj_string_copy(vm, buf, (int)strlen(buf)));
    free(buf);
    return v;
}

static MsValue ms_io_read_bytes(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "io.read_bytes: expected string path");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, "rb");
#else
    fp = fopen(path, "rb");
#endif
    if (!fp) {
        ms_vm_runtime_error(vm, "io.read_bytes: cannot open '%s': %s", path, io_strerror(errno));
        return MS_NIL_VAL();
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz < 0) { fclose(fp); ms_vm_runtime_error(vm, "io.read_bytes: ftell failed"); return MS_NIL_VAL(); }
    uint8_t* buf = (uint8_t*)ms_reallocate(vm, NULL, 0, (size_t)sz == 0 ? 1 : (size_t)sz);
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    MsObjBuffer* b = ms_obj_buffer_from_bytes(vm, buf, rd);
    ms_reallocate(vm, buf, (size_t)sz == 0 ? 1 : (size_t)sz, 0);
    return MS_OBJ_VAL(b);
}

static MsValue ms_io_write_file(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "io.write_file: expected (path, text)");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    MsObjString* s   = MS_AS_STRING(argv[1]);
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, "w");
#else
    fp = fopen(path, "w");
#endif
    if (!fp) {
        ms_vm_runtime_error(vm, "io.write_file: cannot open '%s': %s", path, io_strerror(errno));
        return MS_NIL_VAL();
    }
    fwrite(s->data, 1, (size_t)s->length, fp);
    fclose(fp);
    return MS_NIL_VAL();
}

static MsValue ms_io_write_bytes(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_BUFFER(argv[1])) {
        ms_vm_runtime_error(vm, "io.write_bytes: expected (path, buffer)");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    MsObjBuffer* b   = MS_AS_BUFFER(argv[1]);
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, "wb");
#else
    fp = fopen(path, "wb");
#endif
    if (!fp) {
        ms_vm_runtime_error(vm, "io.write_bytes: cannot open '%s': %s", path, io_strerror(errno));
        return MS_NIL_VAL();
    }
    if (b->len > 0) fwrite(b->data, 1, b->len, fp);
    fclose(fp);
    return MS_NIL_VAL();
}

static MsValue ms_io_append_file(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "io.append_file: expected (path, text)");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    MsObjString* s   = MS_AS_STRING(argv[1]);
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, "a");
#else
    fp = fopen(path, "a");
#endif
    if (!fp) {
        ms_vm_runtime_error(vm, "io.append_file: cannot open '%s': %s", path, io_strerror(errno));
        return MS_NIL_VAL();
    }
    fwrite(s->data, 1, (size_t)s->length, fp);
    fclose(fp);
    return MS_NIL_VAL();
}

static MsValue ms_io_lines(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "io.lines: expected string path");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, "r");
#else
    fp = fopen(path, "r");
#endif
    if (!fp) {
        ms_vm_runtime_error(vm, "io.lines: cannot open '%s': %s", path, io_strerror(errno));
        return MS_NIL_VAL();
    }
    MsObjList* list = ms_obj_list_new(vm);
    char chunk[512];
    char* line = NULL;
    size_t total = 0;
    size_t cap   = 0;
    while (fgets(chunk, sizeof(chunk), fp)) {
        size_t len = strlen(chunk);
        if (total + len + 1 > cap) {
            size_t new_cap = cap < 512 ? 512 : cap * 2;
            while (new_cap < total + len + 1) new_cap *= 2;
            line = (char*)ms_reallocate(vm, line, cap, new_cap);
            cap = new_cap;
        }
        memcpy(line + total, chunk, len);
        total += len;
        if (total > 0 && line[total - 1] == '\n') {
            /* strip \r\n or \n */
            if (total >= 2 && line[total - 2] == '\r') total -= 2;
            else total -= 1;
            line[total] = '\0';
            MsValue sv = MS_OBJ_VAL(ms_obj_string_copy(vm, line, (int)total));
            ms_value_array_push(&list->items, sv);
            total = 0;
        }
    }
    /* last line without trailing newline */
    if (total > 0) {
        line[total] = '\0';
        MsValue sv = MS_OBJ_VAL(ms_obj_string_copy(vm, line, (int)total));
        ms_value_array_push(&list->items, sv);
    }
    if (line) ms_reallocate(vm, line, cap, 0);
    fclose(fp);
    return MS_OBJ_VAL(list);
}

static MsValue ms_io_open(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "io.open: expected (path, mode)");
        return MS_NIL_VAL();
    }
    const char* path     = MS_AS_CSTRING(argv[0]);
    const char* mode_str = MS_AS_CSTRING(argv[1]);
    MsFileMode fm = (strchr(mode_str, 'b') != NULL) ? MS_FILE_BINARY : MS_FILE_TEXT;
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, mode_str);
#else
    fp = fopen(path, mode_str);
#endif
    if (!fp) {
        ms_vm_runtime_error(vm, "io.open: cannot open '%s': %s", path, io_strerror(errno));
        return MS_NIL_VAL();
    }
    MsObjFile* f = ms_obj_file_new(vm, fp, fm, true);
    /* store mode string (truncate to 7 chars max) */
    size_t mlen = strlen(mode_str);
    if (mlen > 7) mlen = 7;
    memcpy(f->mode_str, mode_str, mlen);
    f->mode_str[mlen] = '\0';
    return MS_OBJ_VAL(f);
}

/* ---- Async file IO (CAPI-07) ------------------------------------ */

static MsValue ms_io_read_file_async(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "read_file_async: expected string path");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_vm_pin_future(vm, fut);

    MsJob* job = (MsJob*)calloc(1, sizeof(MsJob));
    job->kind          = MS_JOB_READ_FILE;
    job->path          = ms_strdup(path);
    job->future_opaque = fut;
    ms_threadpool_submit(&vm->threadpool, job);
    return MS_OBJ_VAL((MsObject*)fut);
}

static MsValue ms_io_write_file_async(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "write_file_async: expected path and content strings");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    MsObjString* content = MS_AS_STRING(argv[1]);
    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_vm_pin_future(vm, fut);

    MsJob* job = (MsJob*)calloc(1, sizeof(MsJob));
    job->kind      = MS_JOB_WRITE_FILE;
    job->path      = ms_strdup(path);
    job->write_len = (size_t)content->length;
    if (content->length > 0) {
        job->write_buf = (char*)malloc(job->write_len);
        if (!job->write_buf) {
            ms_vm_unpin_future(vm, fut);
            free(job->path);
            free(job);
            ms_vm_runtime_error(vm, "write_file_async: out of memory");
            return MS_NIL_VAL();
        }
        memcpy(job->write_buf, content->data, job->write_len);
    }
    job->future_opaque = fut;
    ms_threadpool_submit(&vm->threadpool, job);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- Module registration ---------------------------------------- */

static const MsNativeDef io_defs[] = {
    { "read_file",        ms_io_read_file,        1 },
    { "read_bytes",       ms_io_read_bytes,       1 },
    { "write_file",       ms_io_write_file,       2 },
    { "write_bytes",      ms_io_write_bytes,      2 },
    { "append_file",      ms_io_append_file,      2 },
    { "lines",            ms_io_lines,            1 },
    { "read_file_async",  ms_io_read_file_async,  1 },
    { "write_file_async", ms_io_write_file_async, 2 },
    { "open",             ms_io_open,             2 },
    { NULL, NULL, 0 }
};

void ms_module_io_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, io_defs);
    /* Standard streams: owns_fp=false so GC does not fclose them */
    ms_module_export_value(vm, mod, "stdin",
        MS_OBJ_VAL(ms_obj_file_new(vm, stdin,  MS_FILE_TEXT, false)));
    ms_module_export_value(vm, mod, "stdout",
        MS_OBJ_VAL(ms_obj_file_new(vm, stdout, MS_FILE_TEXT, false)));
    ms_module_export_value(vm, mod, "stderr",
        MS_OBJ_VAL(ms_obj_file_new(vm, stderr, MS_FILE_TEXT, false)));
}
