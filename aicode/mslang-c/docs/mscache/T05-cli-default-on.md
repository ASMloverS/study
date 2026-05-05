# T05: CLI 默认开启 + `--no-cache` / `--cache-mode`

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让普通执行（非 benchmark）路径默认走缓存；移除 `--with-cache`；新增 `--no-cache` 和 `--cache-mode=mtime|hash`；写失败 silent。完成后 `./build/mslang-c script.ms` 即触发缓存写入与读取。

**Architecture:** 只修改 `src/main.c`。普通执行路径抽取为 `run_script` helper，cache ON 时调用 `ms_compile_cached(vm, script, cache_flags)` 并手动 wrap fn；cache OFF 时走原有 `ms_vm_interpret`。benchmark 路径同步更新。

**Tech Stack:** C11, `ms_obj_closure_new`, `ms_vm_run`

**前置条件：** T03（新 ms_compile_cached 签名）、T04（hash 模式）已完成

---

## Files

- Modify: `src/main.c`

---

- [ ] **Step 1: 在 `src/main.c` 中更新 flag 解析区块**

找到 `main` 函数中的 flag 解析区块（约 `main.c:187-210`），将整段替换为：

```c
    int   bench_n      = 0;
    bool  flag_stats   = false;
    bool  flag_json    = false;
    bool  flag_no_cache = false;           /* --no-cache */
    uint32_t cache_flags = MS_CACHE_MTIME; /* --cache-mode=mtime|hash */
    const char* script = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--benchmark") == 0 && i + 1 < argc) {
            bench_n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--stats") == 0) {
            flag_stats = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            flag_json = true;
        } else if (strcmp(argv[i], "--no-cache") == 0) {
            flag_no_cache = true;
        } else if (strncmp(argv[i], "--cache-mode=", 13) == 0) {
            const char* mode = argv[i] + 13;
            if (strcmp(mode, "hash") == 0)
                cache_flags = MS_CACHE_HASH;
            else if (strcmp(mode, "mtime") == 0)
                cache_flags = MS_CACHE_MTIME;
            else {
                fprintf(stderr, "Unknown cache mode: %s (use mtime or hash)\n", mode);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            script = argv[i];
        } else {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            return 1;
        }
    }

    if (!script) {
        fprintf(stderr,
            "Usage: mslang-c [--benchmark N] [--stats] [--json]\n"
            "                [--no-cache] [--cache-mode=mtime|hash]\n"
            "                [--version] <script>\n");
        return 1;
    }
```

- [ ] **Step 2: 更新非 benchmark 执行路径**

找到非 benchmark 执行区块（约 `main.c:222-246`）：

```c
    /* Non-benchmark: simple run */
    if (bench_n <= 0) {
        MsVM vm;
        ms_vm_init(&vm);
        MsInterpretResult result = ms_vm_interpret(&vm, src, script);
        ...
```

将该区块完整替换为：

```c
    /* Non-benchmark: simple run */
    if (bench_n <= 0) {
        MsVM vm;
        ms_vm_init(&vm);
        MsInterpretResult result;

        if (!flag_no_cache) {
            /* Default: cache path — source file not read by us */
            MsObjFunction* fn = ms_compile_cached(&vm, script, cache_flags);
            if (!fn) {
                ms_vm_free(&vm);
                free(src);
                return 1;
            }
            MsObjClosure* cl = ms_obj_closure_new(&vm, fn);
            vm.frames[0].closure = cl;
            vm.frames[0].ip      = fn->chunk.code;
            vm.frames[0].slots   = vm.stack;
            vm.frame_count       = 1;
            int need = fn->max_stack_size + 1;
            if (need < 1) need = 1;
            vm.stack_top = vm.stack + need;
            result = ms_vm_run(&vm);
        } else {
            /* --no-cache: classic interpret path (needs source) */
            if (!src) { src = read_file(script); }
            if (!src) { ms_vm_free(&vm); return 1; }
            result = ms_vm_interpret(&vm, src, script);
        }

#ifdef MSLANG_VM_STATS
        if (flag_stats && result == MS_INTERPRET_OK) {
            MsVMStats st;
            ms_vm_get_stats(&vm, &st);
            fprintf(stderr, "stats: instr=%" PRIu64 " minor_gc=%" PRIu64
                    " major_gc=%" PRIu64 " incr_step=%" PRIu64
                    " deopt=%" PRIu64 " peak_bytes=%zu peak_frames=%d"
                    " live_obj=%d\n",
                    st.instruction_count, st.minor_gc_count, st.major_gc_count,
                    st.incremental_step_count, st.deopt_event_count,
                    st.bytes_allocated_peak, st.peak_frame_count,
                    st.live_objects_after_final_gc);
        }
#else
        MS_UNUSED(flag_stats);
#endif
        MS_UNUSED(flag_json);
        ms_vm_free(&vm);
        free(src);
        return result == MS_INTERPRET_OK ? 0 : 1;
    }
```

- [ ] **Step 3: 延迟 `read_file` 调用（只在需要时读取源文件）**

找到 `src` 的读取代码（在 flag 解析之后、非 benchmark 区块之前）：

```c
    char* src = read_file(script);
    if (!src) return 1;
```

替换为（延迟读取：cache 模式下不需要 src）：

```c
    /* Only read source for --no-cache or benchmark no-cache path */
    char* src = NULL;
    if (flag_no_cache) {
        src = read_file(script);
        if (!src) return 1;
    }
```

- [ ] **Step 4: 更新 benchmark 路径中 `flag_cache` 引用**

找到 benchmark 循环中：

```c
        if (flag_cache) {
            res = run_one_cached(script, 0, &s);
            if (i == 0) compile_cold = s.compile_ms;
        } else {
            res = run_one_nocache(src, script, &s);
        }
```

替换为：

```c
        if (!flag_no_cache) {
            res = run_one_cached(script, cache_flags, &s);
            if (i == 0) compile_cold = s.compile_ms;
        } else {
            if (!src) { src = read_file(script); if (!src) { free(compile_times); free(interpret_times); return 1; } }
            res = run_one_nocache(src, script, &s);
        }
```

同样找到 benchmark 汇总区块中依赖 `flag_cache` 的 `if` 分支并更新：

```c
    /* 旧: if (flag_cache) { ... } */
    /* 新: */
    if (!flag_no_cache) {
        printf("compile_cold=%.3f ms  compile_warm=%.3f ms\n",
               compile_cold, compile_warm);
    }
```

以及 JSON 输出中：

```c
    /* 旧: if (flag_cache) { ... } */
    /* 新: */
    if (!flag_no_cache) {
        printf(",\"compile_ms_cold\":%.3f,\"compile_ms_warm\":%.3f",
               compile_cold, compile_warm);
    }
```

同时找到 `compile_warm` 的计算行：

```c
    double compile_warm = (bench_n > 1 && flag_cache) ? compile_times[bench_n - 1] : 0.0;
```

替换为：

```c
    double compile_warm = (bench_n > 1 && !flag_no_cache) ? compile_times[bench_n - 1] : 0.0;
```

- [ ] **Step 5: 在 `src/main.c` 顶部添加 serializer 所需的 include（如尚未包含）**

确认文件顶部已有：

```c
#include "ms/serializer.h"
```

（当前已存在，无需修改。）

- [ ] **Step 6: 构建**

```bash
cmake --build build
```

预期：零错误。若出现 `flag_cache undeclared` 或 `src undeclared`，检查步骤 1–4 中所有旧变量是否都已替换。

- [ ] **Step 7: 冒烟测试——默认缓存开启**

```bash
# 清理旧缓存
rm -rf tests/fixtures/__mscache__

# 首次执行：应写缓存
./build/mslang-c tests/fixtures/hello.ms
ls tests/fixtures/__mscache__/
```

预期：`hello.msc` 出现。

```bash
# 第二次执行：mtime fast path（不打开源文件）
./build/mslang-c tests/fixtures/hello.ms
```

预期：正常输出，无错误。

- [ ] **Step 8: 冒烟测试——`--no-cache`**

```bash
./build/mslang-c --no-cache tests/fixtures/hello.ms
```

预期：正常输出，`__mscache__/` 内容**未变化**（不写新缓存）。

- [ ] **Step 9: 冒烟测试——`--cache-mode=hash`**

```bash
rm -rf tests/fixtures/__mscache__
./build/mslang-c --cache-mode=hash tests/fixtures/hello.ms
ls tests/fixtures/__mscache__/
```

预期：`hello.msc` 写出，再次执行仍正常。

- [ ] **Step 10: 冒烟测试——`--version` 仍正常**

```bash
./build/mslang-c --version
```

预期：`mslang-c <version>` 输出。

- [ ] **Step 11: 全量测试不回归**

```bash
cd build && ctest --output-on-failure
```

- [ ] **Step 12: Commit**

```bash
git add src/main.c
git commit -m "✨ feat(cache): default cache ON, add --no-cache / --cache-mode flags, remove --with-cache"
```
