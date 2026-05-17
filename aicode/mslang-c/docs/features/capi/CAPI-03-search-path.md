# CAPI-03: 模块搜索路径（MSLANG_PATH + --module-path）

## 目标

让动态加载（CAPI-04）和文件系统模块加载能够在多个目录中查找，支持：

- 环境变量 `MSLANG_PATH`（冒号/分号分隔）
- CLI 参数 `--module-path=<dir>`（可重复，优先级最高）
- 脚本所在目录（回退，保持现状）

---

## 查找顺序

```
1. --module-path 路径（按 CLI 出现顺序，先出现先查）
2. MSLANG_PATH 中的路径（按列表顺序）
3. 脚本所在目录（当前行为，保持）
4. 当前工作目录（最后回退）
```

---

## `MsVM` 新增字段（`include/ms/vm.h`）

```c
typedef struct MsVM {
    // ...
    char** module_search_paths;   // malloc'd 路径数组（每项 strdup）
    int    module_search_count;
    int    module_search_cap;
} MsVM;
```

`ms_vm_init` 时：
1. 读取 `MSLANG_PATH` 并 split（Unix 用 `:`，Windows 用 `;`），追加到数组
2. `module_search_paths` 初始为 NULL（0 个路径）

`ms_vm_free` 时：逐项 `free`，再 `free(vm->module_search_paths)`。

---

## CLI 解析（`src/main.c`）

```c
// 在 ms_vm_init 之后，ms_vm_interpret 之前处理 argv：
for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--module-path=", 14) == 0) {
        const char* path = argv[i] + 14;
        ms_vm_add_search_path(&vm, path);  // 插入头部（优先于 MSLANG_PATH）
    }
}
```

`ms_vm_add_search_path`（`include/ms/module.h`）：

```c
void ms_vm_add_search_path(MsVM* vm, const char* path);
void ms_vm_prepend_search_path(MsVM* vm, const char* path);
```

`add` 追加（用于 `MSLANG_PATH`），`prepend` 插到头部（用于 `--module-path`）。

---

## `MSLANG_PATH` 初始化（`src/module_loader.c`）

```c
static void load_mslang_path(MsVM* vm) {
    const char* env = getenv("MSLANG_PATH");
    if (!env) return;

    char* copy = strdup(env);
    char* saveptr = NULL;
#ifdef _WIN32
    const char delim[] = ";";
#else
    const char delim[] = ":";
#endif
    char* tok = strtok_r(copy, delim, &saveptr);
    while (tok) {
        ms_vm_add_search_path(vm, tok);
        tok = strtok_r(NULL, delim, &saveptr);
    }
    free(copy);
}
```

在 `ms_vm_init` 末尾（`ms_stdlib_register_all` 之后）调用 `load_mslang_path(vm)`。

---

## 动态加载中的路径遍历（与 CAPI-04 联动）

`ms_dynlib_find_module(vm, name)` 按 `vm->module_search_paths` 依次拼接：

| 平台 | 尝试的文件名 |
|---|---|
| Linux | `lib{name}.so` |
| macOS | `lib{name}.dylib` |
| Windows | `{name}.dll` |

文件系统路径解析（`ms_resolve_path`）不使用 `module_search_paths`——它仍沿用 `from_dir` 相对路径，保持现有行为。`module_search_paths` 只影响动态加载查找。

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `include/ms/module.h` | 增 `ms_vm_add_search_path`、`ms_vm_prepend_search_path` |
| `include/ms/vm.h` | `MsVM` 增 `module_search_paths`/count/cap |
| `src/main.c` | 解析 `--module-path=` 参数 |
| `src/module_loader.c` | 新建；`load_mslang_path` 实现 |
| `src/vm.c` | `ms_vm_init` 末尾调 `load_mslang_path`；`ms_vm_free` 释放路径 |

---

## 测试要点

```bash
# 环境变量
MSLANG_PATH=/tmp/mylibs ./build/mslang-c script.ms

# CLI 参数（优先级高于环境变量）
./build/mslang-c --module-path=/opt/ms --module-path=./extra script.ms
```

```c
// tests/unit/test_capi_search_path.c
// 1. 设 MSLANG_PATH=dir1:dir2，ms_vm_init 后 search_paths 顺序正确
// 2. ms_vm_prepend_search_path 插头部
// 3. Windows 分号分隔正确 split
```
