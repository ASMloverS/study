# T02: `MsMscHeader` v2 + 缓存路径解析

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `MsMscHeader` 从 16 字节扩展到 32 字节（新增 `src_size` / `src_mtime_ns` 字段），更新 `ms_serialize` / `ms_deserialize` 签名，新增 `ms_cache_path_for` 将源文件路径映射到 `__mscache__/<basename>.msc`。完成后 `ms_compile_cached` 已写入 `__mscache__/` 目录（hash 模式，mtime 快速路径在 T03 实现）。

**Architecture:** 纯增量修改 `include/ms/serializer.h` + `src/serializer.c` + `tests/unit/test_serializer.c`。`ms_compile_cached` 外部签名不变（`vm, source, src_path`），仅更新内部实现。`ms_serializer` 新增对 `ms_fs_util` 的依赖（用于 `ms_fs_mkdir`）。

**Tech Stack:** C11, `_Static_assert`, FNV-1a hash

**前置条件：** T01 已完成（`ms_fs_util` 库存在）

---

## Files

- Modify: `include/ms/serializer.h`
- Modify: `src/serializer.c`
- Modify: `tests/unit/test_serializer.c`
- Modify: `CMakeLists.txt` — `ms_serializer` 链接 `ms_fs_util`

---

- [ ] **Step 1: 更新 `tests/unit/test_serializer.c`（先写失败测试）**

用以下内容完整替换 `tests/unit/test_serializer.c`：

```c
#include "test_assert.h"
#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/serializer.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <direct.h>
#  define ms_rmdir _rmdir
#else
#  include <unistd.h>
#  define ms_rmdir rmdir
#endif

/* ---- helpers ---- */

static void cleanup_cache(void) {
    remove("__mscache__/_t02_test.msc");
    ms_rmdir("__mscache__");
    remove("_t02_test.msc");
}

/* ---- tests ---- */

static void test_cache_path_for(void) {
    char out[512];

    /* script with directory and .ms extension */
    TEST_ASSERT(ms_cache_path_for("tests/foo.ms", out, sizeof(out)));
    TEST_ASSERT_STR_EQ(out, "tests/__mscache__/foo.msc");

    /* script without directory */
    TEST_ASSERT(ms_cache_path_for("bar.ms", out, sizeof(out)));
    TEST_ASSERT_STR_EQ(out, "__mscache__/bar.msc");

    /* script without .ms extension */
    TEST_ASSERT(ms_cache_path_for("baz", out, sizeof(out)));
    TEST_ASSERT_STR_EQ(out, "__mscache__/baz.msc");

    /* buffer too small → false */
    char small[10];
    TEST_ASSERT(!ms_cache_path_for("some/long/path/script.ms", small, sizeof(small)));
}

static void test_roundtrip_hash_mode(void) {
    cleanup_cache();

    MsVM vm; ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    const char* src = "fun add(a,b) { return a + b }\nprint(add(1,2))";
    MsObjFunction* fn = ms_compile(&vm, src, "_t02_test.ms", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);

    uint32_t hash = ms_fnv1a(src, (int)strlen(src));

    /* Build a hash-mode header */
    MsMscHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = 'M'; hdr.magic[1] = 'S'; hdr.magic[2] = 'C'; hdr.magic[3] = '\0';
    hdr.version   = MS_MSC_VERSION;
    hdr.flags     = MS_CACHE_HASH;
    hdr.src_hash  = hash;

    TEST_ASSERT(ms_serialize(fn, "_t02_test.msc", &hdr));

    /* correct hash → hit */
    MsObjFunction* fn2 = ms_deserialize(&vm, "_t02_test.msc", 0, 0, hash);
    TEST_ASSERT(fn2 != NULL);
    TEST_ASSERT_EQ(fn2->arity, fn->arity);
    TEST_ASSERT_EQ(fn2->chunk.code_count, fn->chunk.code_count);

    /* wrong hash → miss */
    TEST_ASSERT(ms_deserialize(&vm, "_t02_test.msc", 0, 0, hash + 1) == NULL);

    remove("_t02_test.msc");
    ms_vm_free(&vm);
}

static void test_roundtrip_mtime_mode(void) {
    cleanup_cache();

    MsVM vm; ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    const char* src = "print(42)";
    MsObjFunction* fn = ms_compile(&vm, src, "_t02_mtime.ms", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);

    MsMscHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = 'M'; hdr.magic[1] = 'S'; hdr.magic[2] = 'C'; hdr.magic[3] = '\0';
    hdr.version      = MS_MSC_VERSION;
    hdr.flags        = MS_CACHE_MTIME;
    hdr.src_size     = 9;       /* len("print(42)") */
    hdr.src_mtime_ns = 1234567890000000000LL;

    TEST_ASSERT(ms_serialize(fn, "_t02_mtime.msc", &hdr));

    /* correct size+mtime → hit */
    MsObjFunction* fn2 = ms_deserialize(&vm, "_t02_mtime.msc",
                                         9, 1234567890000000000LL, 0);
    TEST_ASSERT(fn2 != NULL);

    /* wrong mtime → miss */
    TEST_ASSERT(ms_deserialize(&vm, "_t02_mtime.msc",
                                9, 1234567890000000001LL, 0) == NULL);

    /* wrong size → miss */
    TEST_ASSERT(ms_deserialize(&vm, "_t02_mtime.msc",
                                10, 1234567890000000000LL, 0) == NULL);

    remove("_t02_mtime.msc");
    ms_vm_free(&vm);
}

static void test_missing_file(void) {
    MsVM vm; ms_vm_init(&vm);
    TEST_ASSERT(ms_deserialize(&vm, "_t02_no_such_file.msc", 0, 0, 0) == NULL);
    ms_vm_free(&vm);
}

static void test_compile_cached_writes_to_mscache(void) {
    cleanup_cache();

    /* Write a minimal .ms source file so ms_compile_cached can read it */
    FILE* f = fopen("_t02_test.ms", "wb");
    TEST_ASSERT(f != NULL);
    const char* src = "print(1)";
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    MsVM vm; ms_vm_init(&vm);
    char* source = ms_read_file("_t02_test.ms");
    TEST_ASSERT(source != NULL);

    MsObjFunction* fn = ms_compile_cached(&vm, source, "_t02_test.ms");
    TEST_ASSERT(fn != NULL);
    free(source);

    /* Cache should have been written to __mscache__/_t02_test.msc */
    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t02_test.msc", &m));
    TEST_ASSERT(m.size > 0);

    cleanup_cache();
    remove("_t02_test.ms");
    ms_vm_free(&vm);
}

int main(void) {
    test_cache_path_for();
    test_roundtrip_hash_mode();
    test_roundtrip_mtime_mode();
    test_missing_file();
    test_compile_cached_writes_to_mscache();
    printf("test_serializer: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 运行，确认编译失败（预期）**

```bash
cmake --build build 2>&1 | head -20
```

预期：编译错误，`MS_CACHE_HASH` / `ms_cache_path_for` / 新 `ms_serialize` 签名未定义。

- [ ] **Step 3: 更新 `include/ms/serializer.h`**

用以下内容完整替换文件：

```c
#pragma once
#include "ms/object.h"
#include "ms/fs_util.h"
#include <stdbool.h>
#include <stdint.h>

#define MS_MSC_MAGIC    "MSC\0"
#define MS_MSC_VERSION  2

#define MS_CACHE_MTIME  0u   /* validate by (src_size, src_mtime_ns) — default */
#define MS_CACHE_HASH   1u   /* validate by FNV-1a hash of source              */

/*
 * 32-byte header, no padding (field order chosen to avoid alignment gaps):
 *   char[4]   offset 0
 *   uint32_t  offset 4
 *   uint64_t  offset 8   (8-byte aligned)
 *   int64_t   offset 16
 *   uint32_t  offset 24
 *   uint32_t  offset 28
 */
typedef struct {
    char     magic[4];       /* "MSC\0"                                        */
    uint32_t version;        /* bytecode format version; mismatch → cache miss  */
    uint64_t src_size;       /* source file size in bytes   (mtime mode)        */
    int64_t  src_mtime_ns;   /* source mtime, nanoseconds   (mtime mode)        */
    uint32_t src_hash;       /* FNV-1a of source            (hash  mode)        */
    uint32_t flags;          /* bit 0: MS_CACHE_MTIME(0) or MS_CACHE_HASH(1)   */
} MsMscHeader;               /* 32 bytes                                        */

struct MsVM;

/*
 * ms_serialize  — write fn + hdr to path.
 * ms_deserialize — load fn from path; validates header against the three
 *                  expected values.  Which values are checked depends on
 *                  hdr.flags stored in the file:
 *                    MTIME: checks src_size + src_mtime_ns
 *                    HASH:  checks src_hash
 *                  Pass 0 for unused parameters.
 * ms_cache_path_for — map "dir/script.ms" → "dir/__mscache__/script.msc".
 *                     Returns false if out buffer is too small.
 * ms_compile_cached — high-level: load from cache or compile + cache.
 *                     (Signature unchanged until T03.)
 */
bool           ms_serialize(MsObjFunction* fn, const char* path,
                             const MsMscHeader* hdr);
MsObjFunction* ms_deserialize(struct MsVM* vm, const char* path,
                               uint64_t src_size, int64_t src_mtime_ns,
                               uint32_t src_hash);
bool           ms_cache_path_for(const char* src_path, char* out, size_t cap);
MsObjFunction* ms_compile_cached(struct MsVM* vm, const char* source,
                                   const char* src_path);
```

- [ ] **Step 4: 更新 `src/serializer.c`**

用以下内容完整替换文件（保留原有宏定义和所有 write_fn / read_fn / collect_fns 实现，只替换 header 操作部分）：

```c
#include "ms/serializer.h"
#include "ms/fs_util.h"
#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/chunk.h"
#include "ms/module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

MsObjFunction* ms_compile_cached(MsVM* vm, const char* source,
                                   const char* src_path) {
    uint32_t hash = ms_fnv1a(source, (int)strlen(source));
    char cache[PATH_MAX];
    if (!ms_cache_path_for(src_path, cache, sizeof(cache))) return NULL;

    /* Create __mscache__/ directory (best effort, silent on failure) */
    int sep = last_sep(cache);
    if (sep > 0) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%.*s", sep, cache);
        ms_fs_mkdir(dir);
    }

    /* Try to load from cache (hash mode: T03 will add mtime fast path) */
    MsObjFunction* fn = ms_deserialize(vm, cache, 0, 0, hash);
    if (fn) return fn;

    /* Compile */
    MsDiagnostic diags[32]; int dc = 0;
    fn = ms_compile(vm, source, src_path, diags, &dc, 32);
    if (!fn) return NULL;

    /* Write cache (hash mode header) */
    MsMscHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = 'M'; hdr.magic[1] = 'S'; hdr.magic[2] = 'C'; hdr.magic[3] = '\0';
    hdr.version  = MS_MSC_VERSION;
    hdr.flags    = MS_CACHE_HASH;
    hdr.src_hash = hash;
    ms_serialize(fn, cache, &hdr);  /* failure is silently ignored */
    return fn;
}
```

- [ ] **Step 5: 在 `CMakeLists.txt` 中为 `ms_serializer` 添加 `ms_fs_util` 依赖**

找到这一行：

```cmake
target_link_libraries(ms_serializer PUBLIC ms_vm)
```

替换为：

```cmake
target_link_libraries(ms_serializer PUBLIC ms_vm ms_fs_util)
```

- [ ] **Step 6: 构建**

```bash
cmake --build build
```

预期：零错误，`_Static_assert` 通过。若报 `static_assert` 未识别，说明编译器不支持 C11 `_Static_assert`——这在项目已有 `set(CMAKE_C_STANDARD 11)` 的情况下不应出现。

- [ ] **Step 7: 运行 test_serializer**

```bash
cd build && ctest -R test_serializer --output-on-failure
```

预期输出：

```
test_serializer: all passed
```

- [ ] **Step 8: 验证 `--with-cache` 写到 `__mscache__/`（如有可用 fixture）**

```bash
./build/mslang-c --benchmark 1 --with-cache tests/fixtures/hello.ms
ls tests/fixtures/__mscache__/
```

预期：`hello.msc` 出现在 `tests/fixtures/__mscache__/` 下。

- [ ] **Step 9: 确认全量测试不回归**

```bash
cd build && ctest --output-on-failure
```

- [ ] **Step 10: Commit**

```bash
git add include/ms/serializer.h src/serializer.c \
        tests/unit/test_serializer.c CMakeLists.txt
git commit -m "✨ feat(cache): extend MsMscHeader to v2 (32B) and add ms_cache_path_for"
```
