# T03: mtime 快速路径 + 原子写入

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重写 `ms_compile_cached` 为新签名 `(vm, src_path, flags)`：mtime 模式下仅 stat 源文件、无需读取源文件内容即可命中缓存；写缓存时通过临时文件 + 原子 rename 保证并发安全。同步更新 `main.c` 中调用方。

**Architecture:** `ms_compile_cached` 新签名取代旧签名 `(vm, source, src_path)`。mtime 快速路径：`stat → 读 header → 比对 (size, mtime_ns) → 命中则 deserialize，不打开源文件`。原子写：先写 `<cache>.tmp.<pid>`，成功后 rename；任何失败 silent 忽略。

**Tech Stack:** C11, `getpid()` (POSIX) / `GetCurrentProcessId()` (Windows)

**前置条件：** T01（fs_util）、T02（v2 header + cache_path_for）已完成

---

## Files

- Modify: `include/ms/serializer.h` — 更新 `ms_compile_cached` 签名
- Modify: `src/serializer.c` — 重写 `ms_compile_cached`
- Modify: `src/main.c` — `run_one_cached` 使用新签名，`--with-cache` 路径更新
- Modify: `tests/unit/test_serializer.c` — 新增 mtime fast-path 测试

---

- [ ] **Step 1: 新增 mtime 快速路径测试（追加到 `tests/unit/test_serializer.c`）**

在 `test_compile_cached_writes_to_mscache` 函数**之后**、`main` 函数**之前**插入：

```c
static void test_mtime_fastpath_hit(void) {
    /* Write source file */
    FILE* f = fopen("_t03_fast.ms", "wb");
    TEST_ASSERT(f != NULL);
    const char* src = "print(99)";
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    /* First run: compiles + writes mtime-mode cache */
    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t03_fast.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    /* Cache file must exist */
    MsFileMeta cm;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t03_fast.msc", &cm));

    /* Second run: must hit cache (source not needed — we delete it to prove it) */
    /* NOTE: We can't truly delete source and still let compile happen on miss,
       so we verify indirectly: the function is returned and the cache file
       mtime is unchanged (no rewrite happened). */
    MsFileMeta cm2_before;
    ms_fs_stat("__mscache__/_t03_fast.msc", &cm2_before);

    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t03_fast.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn2 != NULL);
    ms_vm_free(&vm2);

    MsFileMeta cm2_after;
    ms_fs_stat("__mscache__/_t03_fast.msc", &cm2_after);
    /* Cache file should not have been rewritten (mtime unchanged) */
    TEST_ASSERT_EQ(cm2_before.mtime_ns, cm2_after.mtime_ns);

    remove("_t03_fast.ms");
    remove("__mscache__/_t03_fast.msc");
}

static void test_mtime_fastpath_miss_on_mtime_change(void) {
    /* Write source file */
    FILE* f = fopen("_t03_stale.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(1)", 1, 8, f);
    fclose(f);

    /* First run */
    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t03_stale.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    MsFileMeta cm1;
    ms_fs_stat("__mscache__/_t03_stale.msc", &cm1);

    /* Overwrite source with different content (changes size → cache miss) */
    f = fopen("_t03_stale.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(2)", 1, 8, f);  /* same size but different content */
    fclose(f);

    /* Touch source to change mtime (on fast filesystems mtime may not change
       within the same second; write different size to guarantee size mismatch) */
    f = fopen("_t03_stale.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(2) /* extra */", 1, 20, f);  /* different size */
    fclose(f);

    /* Second run: size changed → cache miss → recompile → new cache written */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t03_stale.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn2 != NULL);
    ms_vm_free(&vm2);

    MsFileMeta cm2;
    ms_fs_stat("__mscache__/_t03_stale.msc", &cm2);
    /* Cache was rewritten → file size changed (new source is bigger) */
    TEST_ASSERT(cm2.size != cm1.size || cm2.mtime_ns != cm1.mtime_ns);

    remove("_t03_stale.ms");
    remove("__mscache__/_t03_stale.msc");
}
```

また在 `main` 函数中追加调用：

```c
    test_mtime_fastpath_hit();
    test_mtime_fastpath_miss_on_mtime_change();
```

- [ ] **Step 2: 运行，确认测试失败（预期——新签名还未实现）**

```bash
cmake --build build 2>&1 | head -20
```

预期：编译错误，`ms_compile_cached` 参数数量不匹配。

- [ ] **Step 3: 更新 `include/ms/serializer.h` 中 `ms_compile_cached` 签名**

将：

```c
MsObjFunction* ms_compile_cached(struct MsVM* vm, const char* source,
                                   const char* src_path);
```

替换为：

```c
/*
 * ms_compile_cached — load from __mscache__/<basename>.msc or compile + cache.
 *   flags: MS_CACHE_MTIME (default) or MS_CACHE_HASH
 *   On cache hit (mtime mode): source file is never opened.
 *   Write failures are silently ignored.
 */
MsObjFunction* ms_compile_cached(struct MsVM* vm, const char* src_path,
                                   uint32_t flags);
```

- [ ] **Step 4: 重写 `src/serializer.c` 中的 `ms_compile_cached`**

在文件末尾，将整个 `ms_compile_cached` 函数替换为：

```c
#ifdef _WIN32
#  include <windows.h>   /* GetCurrentProcessId */
#else
#  include <unistd.h>    /* getpid */
#endif

MsObjFunction* ms_compile_cached(MsVM* vm, const char* src_path, uint32_t flags) {
    char cache[PATH_MAX];
    if (!ms_cache_path_for(src_path, cache, sizeof(cache))) return NULL;

    /* Stat source file — always needed (size/mtime for mtime mode, or
       existence check for hash mode) */
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

    /* Try cache */
    MsObjFunction* fn = ms_deserialize(vm, cache,
                                        has_meta ? meta.size     : 0,
                                        has_meta ? meta.mtime_ns : 0,
                                        hash);
    if (fn) { free(source); return fn; }  /* cache hit */

    /* Cache miss: read source if not already read (mtime mode) */
    if (!source) {
        source = ms_read_file(src_path);
        if (!source) return NULL;
        /* Re-stat in case file appeared after first stat attempt */
        if (!has_meta) has_meta = ms_fs_stat(src_path, &meta);
        /* Compute hash for header even in mtime mode (stored as 0 — unused
           for validation but keeps the field deterministic) */
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
```

> **注意**：`#include <windows.h>` / `#include <unistd.h>` 放在文件顶部 `#include` 区块中（与其他平台头文件一起），而不是在函数内部。上面为了可读性放在一起展示。

- [ ] **Step 5: 更新 `src/main.c` 中 `run_one_cached` 函数**

将 `run_one_cached` 完整替换为（去掉 `source` 参数，使用新签名）：

```c
static MsInterpretResult run_one_cached(const char* path, uint32_t cache_flags,
                                         RunSample* s) {
    double t0, t1, t2;
    MsVM vm;
    ms_vm_init(&vm);

    t0 = get_time_ms();
    MsObjFunction* fn = ms_compile_cached(&vm, path, cache_flags);
    t1 = get_time_ms();
    s->compile_ms = t1 - t0;

    MsInterpretResult res = MS_INTERPRET_OK;
    if (!fn) {
        res = MS_INTERPRET_COMPILE_ERROR;
    } else {
        MsObjClosure* cl = ms_obj_closure_new(&vm, fn);
        vm.frames[0].closure = cl;
        vm.frames[0].ip      = fn->chunk.code;
        vm.frames[0].slots   = vm.stack;
        vm.frame_count       = 1;
        int need = fn->max_stack_size + 1;
        if (need < 1) need = 1;
        vm.stack_top = vm.stack + need;
        double r0 = get_time_ms();
        res = ms_vm_run(&vm);
        t2 = get_time_ms();
        MS_UNUSED(r0);
        s->interpret_ms = t2 - t1;
    }
    ms_vm_free(&vm);
    return res;
}
```

- [ ] **Step 6: 更新 `src/main.c` 中 `run_one_cached` 的调用点**

找到：

```c
        if (flag_cache) {
            res = run_one_cached(src, script, i == 0, &s);
```

替换为：

```c
        if (flag_cache) {
            res = run_one_cached(script, 0, &s);  /* 0 = MS_CACHE_MTIME */
```

（`src` 在 `flag_cache` 模式下不再需要，但 `read_file` 调用保留供 `run_one_nocache` 使用。）

- [ ] **Step 7: 构建**

```bash
cmake --build build
```

预期：零错误。若有 `unused variable 'src'` 警告，在 `flag_cache` 分支前检查 `src` 是否仍被 `run_one_nocache` 引用——若是则保留，若否则移除 `read_file` 调用。

- [ ] **Step 8: 运行 test_serializer**

```bash
cd build && ctest -R test_serializer --output-on-failure
```

预期：

```
test_serializer: all passed
```

- [ ] **Step 9: 冒烟测试 mtime fast path**

```bash
# 首次：编译 + 写缓存（mtime 模式）
./build/mslang-c --benchmark 1 --with-cache tests/fixtures/hello.ms

# 查看缓存文件
ls -la tests/fixtures/__mscache__/hello.msc

# 第二次：应命中 mtime fast path（compile_ms 应接近 0）
./build/mslang-c --benchmark 1 --with-cache tests/fixtures/hello.ms
```

预期：第二次 `compile_ms` 显著低于第一次（<0.5 ms vs 数 ms）。

- [ ] **Step 10: 全量测试不回归**

```bash
cd build && ctest --output-on-failure
```

- [ ] **Step 11: Commit**

```bash
git add include/ms/serializer.h src/serializer.c src/main.c \
        tests/unit/test_serializer.c
git commit -m "⚡ perf(cache): mtime fast path + atomic write in ms_compile_cached"
```
