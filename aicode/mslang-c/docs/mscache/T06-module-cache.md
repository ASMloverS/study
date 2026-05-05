# T06: 模块加载器缓存集成

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `import` 进来的模块也走 `__mscache__/` 缓存。`ms_module_load` 中用 `ms_compile_cached` 替换 `ms_read_file` + `ms_compile`，使 import-heavy 脚本在第二次执行时整条 import 链都不触发重编译。

**Architecture:** 只修改 `src/module.c`：删除手动的 `ms_read_file + ms_compile + free(source)` 序列，用 `ms_compile_cached(vm, resolved, 0)` 一行替代。`ms_compile_cached` 在失败时已向 stderr 打印诊断信息，所以模块 loader 不再需要额外的诊断打印循环。循环依赖检测（`MS_MOD_INITIALIZING`）语义不变。

**Tech Stack:** C11

**前置条件：** T03（新 ms_compile_cached 签名）已完成

---

## Files

- Modify: `src/module.c` — 在 `ms_module_load` 中用 `ms_compile_cached` 替换读取+编译序列
- Modify: `tests/unit/test_modules.c` — 新增 import 缓存集成测试（如尚未覆盖）

---

- [ ] **Step 1: 在 `tests/unit/test_modules.c` 末尾新增缓存集成测试**

先查看 `tests/unit/test_modules.c` 的最后几个函数，找到 `main` 函数，在其**之前**插入：

```c
static void test_import_writes_cache(void) {
    /* Write two source files: main imports helper */
#ifdef _WIN32
    _mkdir("_t06_pkg");
#else
    mkdir("_t06_pkg", 0755);
#endif

    FILE* f = fopen("_t06_pkg/helper.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("fun greet() { return \"hi\" }", 1, 27, f);
    fclose(f);

    f = fopen("_t06_main.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("import \"_t06_pkg/helper\"\nprint(greet())", 1, 39, f);
    fclose(f);

    /* Run the script — both files should get cached */
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult res = ms_vm_interpret(&vm, "", "_t06_main.ms");
    /* Note: ms_vm_interpret compiles+runs; for cache path we call ms_compile_cached.
       Use ms_compile_cached directly to test the module loader path. */
    ms_vm_free(&vm);

    /* The proper way: compile_cached triggers ms_module_load internally on import */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn = ms_compile_cached(&vm2, "_t06_main.ms", 0);
    TEST_ASSERT(fn != NULL);

    /* Execute so the import runs and triggers module caching */
    MsObjClosure* cl = ms_obj_closure_new(&vm2, fn);
    vm2.frames[0].closure = cl;
    vm2.frames[0].ip      = fn->chunk.code;
    vm2.frames[0].slots   = vm2.stack;
    vm2.frame_count       = 1;
    int need = fn->max_stack_size + 1;
    if (need < 1) need = 1;
    vm2.stack_top = vm2.stack + need;
    ms_vm_run(&vm2);
    ms_vm_free(&vm2);

    /* Both scripts should have cache files */
    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t06_main.msc", &m));
    TEST_ASSERT(ms_fs_stat("_t06_pkg/__mscache__/helper.msc", &m));

    /* Cleanup */
    remove("__mscache__/_t06_main.msc");
    remove("_t06_pkg/__mscache__/helper.msc");
#ifdef _WIN32
    _rmdir("_t06_pkg/__mscache__");
    _rmdir("_t06_pkg");
#else
    rmdir("_t06_pkg/__mscache__");
    rmdir("_t06_pkg");
#endif
    remove("_t06_main.ms");
}
```

在 `tests/unit/test_modules.c` 的 `main` 函数末尾（`return 0;` 之前）添加：

```c
    test_import_writes_cache();
```

在 `tests/unit/test_modules.c` 顶部添加必要的 includes（若尚未包含）：

```c
#include "ms/serializer.h"
#include "ms/fs_util.h"
#ifdef _WIN32
#  include <direct.h>
#else
#  include <sys/stat.h>
#endif
```

- [ ] **Step 2: 运行 test_modules，确认新测试失败（预期）**

```bash
cmake --build build && cd build && ctest -R test_modules --output-on-failure
```

预期：`test_import_writes_cache` 失败，因为 `ms_module_load` 还未调用 `ms_compile_cached`，所以 `_t06_pkg/__mscache__/helper.msc` 不会被创建。

- [ ] **Step 3: 修改 `src/module.c`**

在文件顶部的 `#include` 区块中追加：

```c
#include "ms/serializer.h"
```

找到 `ms_module_load` 函数中的以下区块（约 `module.c:136-158`）：

```c
    /* Read source */
    char* source = ms_read_file(resolved);
    free(resolved);
    if (!source) {
        ms_vm_runtime_error(vm, "Cannot open module '%s'.", import_path);
        mod->state = MS_MOD_FAILED;
        return NULL;
    }

    /* Compile */
    MsDiagnostic diags[32];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, source, mod->path->data,
                                    diags, &diag_count, 32);
    free(source);
    if (!fn) {
        for (int i = 0; i < diag_count; i++) {
            fprintf(stderr, "[line %d] Error in module: %s\n",
                    diags[i].line, diags[i].message);
        }
        mod->state = MS_MOD_FAILED;
        return NULL;
    }
```

将该整段替换为：

```c
    /* Compile (cached: writes to __mscache__/<module>.msc on first load) */
    MsObjFunction* fn = ms_compile_cached(vm, resolved, 0);
    free(resolved);
    if (!fn) {
        mod->state = MS_MOD_FAILED;
        return NULL;
    }
```

> `ms_compile_cached` 在编译失败时已将诊断信息打印到 stderr（行号 + 路径 + 消息），所以此处无需额外打印。

- [ ] **Step 4: 构建**

```bash
cmake --build build
```

预期：零错误。若提示 `ms_compile_cached` 未声明，确认 `#include "ms/serializer.h"` 已加入 `src/module.c`。

- [ ] **Step 5: 运行 test_modules**

```bash
cd build && ctest -R test_modules --output-on-failure
```

预期：

```
test_modules: all passed
```

- [ ] **Step 6: 端到端 import 缓存验证**

准备一个有 import 的 fixture（或临时写一个）：

```bash
mkdir -p tests/fixtures/pkg

cat > tests/fixtures/pkg/util.ms << 'EOF'
fun double(x) { return x * 2 }
EOF

cat > tests/fixtures/use_import.ms << 'EOF'
import "pkg/util"
print(double(21))
EOF
```

```bash
# 首次：编译 + 写缓存（主文件 + 模块）
./build/mslang-c tests/fixtures/use_import.ms

# 检查缓存
ls tests/fixtures/__mscache__/           # use_import.msc
ls tests/fixtures/pkg/__mscache__/       # util.msc
```

预期：两个 `__mscache__/` 目录均有 `.msc` 文件。

```bash
# 第二次：mtime fast path，不读任何源文件
./build/mslang-c tests/fixtures/use_import.ms
```

预期：正常输出 `42`。

- [ ] **Step 7: 验证模块内容变更触发重编译**

```bash
# 修改模块内容（改变文件大小 → mtime fast path 失效）
echo 'fun double(x) { return x * 2 } /* updated */' > tests/fixtures/pkg/util.ms
./build/mslang-c tests/fixtures/use_import.ms
```

预期：正常输出 `42`，且 `tests/fixtures/pkg/__mscache__/util.msc` 被更新（mtime 变化）。

- [ ] **Step 8: 全量测试不回归**

```bash
cd build && ctest --output-on-failure
```

- [ ] **Step 9: Commit**

```bash
git add src/module.c tests/unit/test_modules.c
git commit -m "⚡ perf(cache): integrate ms_compile_cached into module loader"
```
