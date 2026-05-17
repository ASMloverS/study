# CAPI-01: 内置模块注册表

## 目标

让 `import io`、`import math` 等内置模块在不读取文件系统的情况下被解析，同时保持与用户自定义 `.ms` 模块的完全兼容。

核心思路：在 `MsVM` 上维护一张按名字索引的**内置模块注册表**，`ms_module_load` 在查文件系统之前先查此表。命中时合成一个 `MsObjModule` 并调用对应 `init` 函数填充 exports。

---

## 数据结构

### `MsBuiltinModuleEntry`（`include/ms/module.h`）

```c
typedef void (*MsBuiltinModuleInit)(MsVM* vm, MsObjModule* mod);

typedef struct {
    const char*          name;  // "io"、"math"...（不含 "<builtin:" 前缀）
    MsBuiltinModuleInit  init;  // 懒加载时调用
} MsBuiltinModuleEntry;
```

### `MsVM` 新增字段（`include/ms/vm.h`）

```c
typedef struct MsVM {
    // ... 已有字段 ...

    /* 内置模块注册表 */
    MsBuiltinModuleEntry* builtin_registry;
    int                   builtin_count;
    int                   builtin_cap;
} MsVM;
```

注册表在 `ms_vm_init` 中初始化为 NULL（延迟分配），在 `ms_vm_free` 中 `free(vm->builtin_registry)`。

---

## API（`include/ms/module.h`）

```c
/* 注册一个内置模块。name 必须是字符串字面量或生命周期覆盖 VM 的静态串。*/
void ms_vm_register_builtin_module(MsVM* vm,
                                   const char* name,
                                   MsBuiltinModuleInit init);

/* 按名字查找注册表，未命中返回 NULL。 */
MsBuiltinModuleInit ms_vm_find_builtin_module(MsVM* vm, const char* name);
```

实现放在 `src/module.c`（与 `ms_module_load` 同文件）。

---

## `ms_module_load` 修改（`src/module.c`）

在现有逻辑前插入注册表检查：

```c
MsObjModule* ms_module_load(MsVM* vm,
                              const char* import_path,
                              const char* from_path) {
    /* 1. 从 import_path 提取纯模块名（strip 目录和 .ms 后缀）*/
    const char* module_name = extract_module_name(import_path); // 内部静态函数

    /* 2. 内置模块注册表查找（按纯名字，不经过路径解析）*/
    MsBuiltinModuleInit builtin_init =
        ms_vm_find_builtin_module(vm, module_name);
    if (builtin_init) {
        /* 合成路径，用于 module_cache 去重 */
        char synth_path[256];
        snprintf(synth_path, sizeof(synth_path), "<builtin:%s>", module_name);

        /* cache 命中（同名内置已加载）→ 直接返回 */
        MsObjString* key = ms_obj_string_copy(vm, synth_path,
                                               (int)strlen(synth_path));
        MsValue cached;
        if (ms_table_get(&vm->module_cache, key, &cached))
            return MS_AS_MODULE(cached);

        /* 合成 MsObjModule */
        MsObjString* mod_name = ms_obj_string_copy(vm, module_name,
                                                    (int)strlen(module_name));
        MsObjModule* mod = ms_obj_module_new(vm, mod_name, key);
        mod->state = MS_MOD_INITIALIZING;
        ms_table_set(&vm->module_cache, key, MS_OBJ_VAL(mod));

        /* 调用注册函数填充 exports */
        builtin_init(vm, mod);

        mod->state = MS_MOD_INITIALIZED;
        return mod;
    }

    /* 3. 现有文件系统路径（不变）*/
    // ... 原有逻辑 ...

    /* 4. 动态加载（CAPI-04 实现后插入此处）*/
}
```

`extract_module_name`：从 `import_path` 取最后一段路径分量，去掉 `.ms` 后缀。对 `"io"`、`"net/tcp"` 等均能正确提取 `"io"`、`"tcp"`。

---

## `ms_stdlib_register_all`（`include/ms/stdlib.h`，`src/stdlib/*.c`）

在 `ms_vm_init` 末尾调用：

```c
void ms_stdlib_register_all(MsVM* vm) {
    ms_vm_register_builtin_module(vm, "math",   ms_module_math_init);
    ms_vm_register_builtin_module(vm, "os",     ms_module_os_init);
    ms_vm_register_builtin_module(vm, "time",   ms_module_time_init);
    ms_vm_register_builtin_module(vm, "io",     ms_module_io_init);
    ms_vm_register_builtin_module(vm, "buffer", ms_module_buffer_init);
    ms_vm_register_builtin_module(vm, "hash",   ms_module_hash_init);
    ms_vm_register_builtin_module(vm, "log",    ms_module_log_init);
    ms_vm_register_builtin_module(vm, "net",    ms_module_net_init);
    ms_vm_register_builtin_module(vm, "debug",  ms_module_debug_init);
    ms_vm_register_builtin_module(vm, "gc",     ms_module_gc_init);
}
```

**懒加载**：`ms_stdlib_register_all` 只向表中写入函数指针，**不**调用任何 `init`；`init` 仅在用户第一次 `import` 该模块时执行。

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `include/ms/module.h` | 增 `MsBuiltinModuleEntry`、`ms_vm_register_builtin_module`、`ms_vm_find_builtin_module` |
| `include/ms/vm.h` | `MsVM` 增 `builtin_registry`/`builtin_count`/`builtin_cap` |
| `include/ms/stdlib.h` | 新建；声明 `ms_stdlib_register_all` 与各 `ms_module_*_init` |
| `src/module.c` | `ms_module_load` 插入步骤 2；新增 `ms_vm_register_builtin_module`/`find` |
| `src/vm.c` | `ms_vm_init` 末尾调 `ms_stdlib_register_all`；`ms_vm_free` 释放 registry |

---

## 测试要点

```c
// tests/unit/test_capi_registry.c
// 1. 注册一个 mock 内置模块，import 后能取到 exports 中的函数
// 2. 同名模块只初始化一次（init 调用次数断言）
// 3. 内置模块优先于同名文件系统模块（放一个同名 .ms 在 from_dir）
```

```ms
// tests/fixtures/stdlib_registry_basic.ms
import math
print(math.PI)   // 3.14159...
```
