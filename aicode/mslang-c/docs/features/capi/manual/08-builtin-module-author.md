# 08 — 内置模块作者指南

本章面向直接在 mslang-c 源码树中编写标准库模块的开发者，
说明内置模块与第三方动态扩展的关键差异，以及 `MsNativeDef`、
`ms_stdlib_register_all` 等内部 API 的使用方式。

---

## 1. 内置模块 vs 动态扩展

| 维度 | 内置模块 | 动态扩展 |
|---|---|---|
| 编译方式 | 静态链接进主程序 | 独立共享库（`.so`/`.dll`/`.dylib`） |
| 头文件访问 | 可 `#include` 内部头（`ms/object.h`、`ms/stdlib/objbuffer.h` …） | 仅限公开头（`ms/module.h`、`ms/value.h`、`ms/common.h`） |
| 对象类型 | 可新增/直接构造 `ObjFile*`、`ObjBuffer*` | 只能用 `userdata_new` 包装 C 指针 |
| 注册方式 | `ms_stdlib_register_all` 中写死 | `ms_module_init` 动态加载 |
| 适用场景 | 语言 stdlib、对性能/ABI 有严格要求 | 第三方、许可证隔离、用户分发 |

**何时选内置模块**：依赖 VM 内部对象（`ObjFile`/`ObjBuffer`/`ObjSocket`）、需要直接调用 `ms_reallocate` / `ms_mark_object`、或许可证不允许独立分发动态库时。

---

## 2. `MsNativeDef` 表式注册

内置模块推荐使用**表式**注册——声明 `MsNativeDef[]` 数组加终止哨兵，再调用 `ms_module_register_natives`：

```c
/* src/stdlib/math.c */
#include "ms/module.h"   /* MsNativeDef, ms_module_register_natives */
#include "ms/value.h"
#include <math.h>

static MsValue ms_math_sqrt(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(sqrt(MS_AS_NUMBER(argv[0])));
}

static MsValue ms_math_sin(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(sin(MS_AS_NUMBER(argv[0])));
}

static const MsNativeDef kMathDefs[] = {
    {"sqrt", ms_math_sqrt, 1},
    {"sin",  ms_math_sin,  1},
    {"cos",  ms_math_cos,  1},
    {NULL, NULL, 0}   /* 哨兵：name == NULL 时停止遍历 */
};

void ms_module_math_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, kMathDefs);
    ms_module_export_value(vm, mod, "PI",  MS_NUMBER_VAL(M_PI));
    ms_module_export_value(vm, mod, "INF", MS_NUMBER_VAL(INFINITY));
}
```

与动态扩展的 `api->def_native` 对比：

| | 动态扩展 | 内置模块 |
|---|---|---|
| 单个注册 | `api->def_native(vm, mod, name, fn, arity)` | `ms_module_def_native(vm, mod, name, fn, arity)` |
| 批量注册 | — | `ms_module_register_natives(vm, mod, defs)` |
| 导出任意值 | `api->export_value(vm, mod, name, val)` | `ms_module_export_value(vm, mod, name, val)` |

`arity = -1` 表示可变参，被调用时 VM 不限制 `argc`。

---

## 3. 注册到 `ms_stdlib_register_all`

内置模块通过 `ms_stdlib_register_all`（`src/stdlib_register.c`）向 VM 注册——只写入函数指针，**不**立即调用 `init`；模块在用户第一次 `import` 时才懒初始化：

```c
/* src/stdlib_register.c */
#include "ms/vm.h"
#include "ms/module.h"

void ms_stdlib_register_all(MsVM* vm) {
    ms_vm_register_builtin_module(vm, "math",   ms_module_math_init);
    ms_vm_register_builtin_module(vm, "io",     ms_module_io_init);
    ms_vm_register_builtin_module(vm, "buffer", ms_module_buffer_init);
    /* … 其他内置模块 … */
}
```

`ms_stdlib_register_all` 在 `ms_vm_init` 末尾调用一次。新增内置模块只需在此追加一行，再在 `include/ms/stdlib_register.h` 中声明对应 `ms_module_*_init` 原型即可。

---

## 4. 直接构造 `ObjFile*` / `ObjBuffer*`

内置模块可 `#include` 内部头并直接构造 VM 对象，用 `MS_OBJ_VAL()` 包装后返回：

### 4.1 构造 `ObjFile`

```c
#include "ms/stdlib/objfile.h"   /* MsObjFile, ms_obj_file_new */
#include <stdio.h>

static MsValue ms_io_open(MsVM* vm, int argc, MsValue* argv) {
    /* ... 参数校验 ... */
    const char* path = MS_AS_CSTRING(argv[0]);
    const char* mode = MS_AS_CSTRING(argv[1]);

    FILE* fp = fopen(path, mode);
    if (!fp) {
        /* 向 .ms 端抛出 IOError */
        return MS_ERROR_VAL(ms_error_new(vm, "IOError",
            ms_obj_string_copyf(vm, "cannot open '%s': %s", path, strerror(errno))));
    }

    MsFileMode fmode = (strchr(mode, 'b') != NULL) ? MS_FILE_BINARY : MS_FILE_TEXT;
    MsObjFile* f = ms_obj_file_new(vm, fp, fmode);
    return MS_OBJ_VAL(f);   /* 包装为 MsValue 返回给 .ms */
}
```

### 4.2 构造 `ObjBuffer`

```c
#include "ms/stdlib/objbuffer.h"   /* MsObjBuffer, ms_obj_buffer_new */

static MsValue ms_buffer_new(MsVM* vm, int argc, MsValue* argv) {
    size_t cap = (argc >= 1 && MS_IS_INT(argv[0]))
                 ? (size_t)MS_AS_INT(argv[0]) : 64;
    MsObjBuffer* b = ms_obj_buffer_new(vm, cap);
    return MS_OBJ_VAL(b);
}

static MsValue ms_buffer_from_str(MsVM* vm, int argc, MsValue* argv) {
    /* ... 校验 argv[0] 为 string ... */
    ObjString* s   = MS_AS_STRING(argv[0]);
    MsObjBuffer* b = ms_obj_buffer_from_bytes(vm,
                         (const uint8_t*)s->chars, (size_t)s->length);
    return MS_OBJ_VAL(b);
}
```

GC 会自动追踪 `MsObjFile` / `MsObjBuffer`（`vm_gc.c` 中已有对应 case），内置模块作者无需手动管理生命周期。

---

## 5. `.ms` 侧访问

内置模块中 `ObjFile` 和 `ObjBuffer` 支持方法语法（由 `ms_builtin_invoke` 分发）：

```ms
import io
import buffer

var f = io.open("data.bin", "rb")
var b = buffer.from_str(f.read())   # ObjFile.read() → ObjBuffer
print(b.len())
f.close()
```

第三方扩展无法得到此能力（v1 `MsModuleApi` 不含 `is_file`/`is_buffer`），参见第 07 章。

---

## 进一步阅读

- **MsNativeDef 完整规格**：[CAPI-02-native-def.md](../CAPI-02-native-def.md)
- **内置模块注册表**：[CAPI-01-registry.md](../CAPI-01-registry.md)
- **ObjFile / ObjBuffer 结构**：[CAPI-06-handle-types.md](../CAPI-06-handle-types.md)
- **打包与分发（动态扩展路径）**：[09-packaging-and-distribution.md](09-packaging-and-distribution.md)
