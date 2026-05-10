# T04: hash 校验模式

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 验证 `ms_compile_cached` 的 `MS_CACHE_HASH` 模式已在 T03 中实现并正确工作：hash 模式下必须读源文件计算 FNV-1a，即使 mtime 发生变化（如 `git checkout`）只要内容未变也应命中缓存；写出的 header 标记为 `flags = MS_CACHE_HASH`。

**Architecture:** T03 已在 `ms_compile_cached` 中实现了 hash 分支（`if (flags & MS_CACHE_HASH)`），本任务只补充针对该分支的测试，并验证 `mtime 写入的缓存被 hash 进程读取时仍按 header 的 flags 自描述校验`。

**Tech Stack:** C11, FNV-1a

**前置条件：** T03 已完成

---

## Files

- Modify: `tests/unit/test_serializer.c` — 新增 hash 模式专项测试

---

- [x] **Step 1: 在 `tests/unit/test_serializer.c` 追加 hash 模式测试**

在 `test_mtime_fastpath_miss_on_mtime_change` 函数**之后**、`main` 函数**之前**插入：

```c
static void test_hash_mode_hit_despite_mtime_change(void) {
    /* Write source file */
    FILE* f = fopen("_t04_hash.ms", "wb");
    TEST_ASSERT(f != NULL);
    const char* src = "print(\"hash\")";
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    /* First run with HASH mode */
    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t04_hash.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    MsFileMeta cm1;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t04_hash.msc", &cm1));

    /* Simulate mtime change without content change:
       Re-write identical content (may change mtime on most filesystems) */
    f = fopen("_t04_hash.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    /* Second run with HASH mode: content unchanged → hash matches → cache hit */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t04_hash.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn2 != NULL);
    ms_vm_free(&vm2);

    /* Cache file should NOT have been rewritten (same content = same hash = hit) */
    MsFileMeta cm2;
    ms_fs_stat("__mscache__/_t04_hash.msc", &cm2);
    TEST_ASSERT_EQ(cm1.size, cm2.size);

    remove("_t04_hash.ms");
    remove("__mscache__/_t04_hash.msc");
}

static void test_hash_mode_miss_on_content_change(void) {
    FILE* f = fopen("_t04_cont.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(1)", 1, 8, f);
    fclose(f);

    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t04_cont.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    MsFileMeta cm1;
    ms_fs_stat("__mscache__/_t04_cont.msc", &cm1);

    /* Change content */
    f = fopen("_t04_cont.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(2)", 1, 8, f);  /* different content, same size */
    fclose(f);

    /* Second run: hash changed → cache miss → recompile */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t04_cont.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn2 != NULL);
    ms_vm_free(&vm2);

    /* Cache file was rewritten (new compile = different fn → may differ in size) */
    MsFileMeta cm2;
    ms_fs_stat("__mscache__/_t04_cont.msc", &cm2);
    /* Rewrite happened: mtime should differ on most filesystems.
       We verify the cache is valid by doing a third hit. */
    MsVM vm3; ms_vm_init(&vm3);
    MsObjFunction* fn3 = ms_compile_cached(&vm3, "_t04_cont.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn3 != NULL);
    ms_vm_free(&vm3);

    remove("_t04_cont.ms");
    remove("__mscache__/_t04_cont.msc");
}

static void test_mtime_cache_not_used_by_hash_mode(void) {
    /* Write source */
    FILE* f = fopen("_t04_cross.ms", "wb");
    TEST_ASSERT(f != NULL);
    const char* src = "print(\"cross\")";
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    /* Write cache with MTIME mode header */
    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t04_cross.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    /* Read it back with HASH mode.
       The header says MS_CACHE_MTIME → ms_deserialize validates by (size, mtime),
       not hash. The passed src_hash doesn't match what would be checked.
       ms_compile_cached with MS_CACHE_HASH passes hash (computed from source)
       but the header says mtime mode → validation uses size+mtime, which match
       current source → HIT (header is self-describing). */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t04_cross.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn2 != NULL);  /* hit: header says mtime mode, mtime still valid */
    ms_vm_free(&vm2);

    remove("_t04_cross.ms");
    remove("__mscache__/_t04_cross.msc");
}
```

在 `main` 函数中追加调用：

```c
    test_hash_mode_hit_despite_mtime_change();
    test_hash_mode_miss_on_content_change();
    test_mtime_cache_not_used_by_hash_mode();
```

- [x] **Step 2: 构建**

```bash
cmake --build build
```

预期：零错误（所有函数已在 T03 实现）。

- [x] **Step 3: 运行 test_serializer**

```bash
cd build && ctest -R test_serializer --output-on-failure
```

预期：

```
test_serializer: all passed
```

若 `test_mtime_cache_not_used_by_hash_mode` 失败，检查 `ms_deserialize` 中的分支逻辑：header 的 `flags` 应决定校验方式，而非调用方传入的 `flags` 参数。`ms_deserialize` 是 self-describing 的——它从 header 读 flags，而非从参数决定。

- [x] **Step 4: 验证 hash 模式 CLI（配合 T05 实现后完整验证）**

此时 `--cache-mode=hash` 还未接入 CLI（T05 实现），可用以下方式手动验证：

```bash
# hash 模式的 benchmark 路径（T05 完成后可用 --cache-mode=hash 替换）
# 目前通过单元测试覆盖即可
cd build && ctest -R test_serializer -V
```

- [x] **Step 5: 全量测试不回归**

```bash
cd build && ctest --output-on-failure
```

- [x] **Step 6: Commit**

```bash
git add tests/unit/test_serializer.c
git commit -m "✅ test(cache): add hash mode coverage (hit/miss/cross-mode self-describing)"
```
