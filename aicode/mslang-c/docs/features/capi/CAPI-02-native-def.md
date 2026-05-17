# CAPI-02: MsNativeDef + 模块函数注册 API

## 目标

提供两种风格让模块作者向 `MsObjModule` 注册原生函数和导出值：

- **风格 A（表式）**：`MsNativeDef[]` 数组 + `ms_module_register_natives`，声明式、紧凑
- **风格 B（逐函数）**：`ms_module_def_native`，与现有 `ms_vm_define_native` 一脉相承

底层只有逐函数原语；表式是其循环包装。

---

## 数据结构（`include/ms/module.h`）

```c
typedef struct {
    const char* name;   // 导出名，NULL 表示结束哨兵
    MsNativeFn  fn;     // 函数指针（MsValue(*)(MsVM*, int, MsValue*)）
    int         arity;  // 期望参数数；-1 = 可变参
} MsNativeDef;
```

---

## API（`include/ms/module.h`）

```c
/* 风格 B — 底层原语 */
void ms_module_def_native(MsObjModule* mod,
                          const char*  name,
                          MsNativeFn   fn,
                          int          arity);

/* 风格 A — 表式便利包装（遍历直到 name == NULL） */
void ms_module_register_natives(MsObjModule*       mod,
                                const MsNativeDef* defs);

/* 导出任意 MsValue（常量、ObjFile、...） */
void ms_module_export_value(MsObjModule* mod,
                            const char*  name,
                            MsValue      value);
```

---

## 实现（`src/module.c`）

```c
void ms_module_def_native(MsObjModule* mod,
                          const char* name,
                          MsNativeFn fn,
                          int arity) {
    /* MsVM 通过 mod->obj 的 next 链可到达；
       但 mod 自身存着 vm 引用吗？—— 不存。
       需要 vm 指针来创建 ObjString / ObjNative。
       方案：通过 (MsVM*)mod->obj.vm 访问（在 MsObject header 中加 vm* 字段）
       或者改签名为 ms_module_def_native(vm, mod, ...) ——
       此处选后者，与现有 ms_vm_define_native 对齐，避免侵入 MsObject。 */
    MsObjString* key = ms_obj_string_copy(vm, name, (int)strlen(name));
    MsObjNative* nat = ms_obj_native_new(vm, fn, key, arity);
    ms_table_set(&mod->exports, key, MS_OBJ_VAL(nat));
}

void ms_module_register_natives(MsVM* vm,
                                MsObjModule* mod,
                                const MsNativeDef* defs) {
    for (const MsNativeDef* d = defs; d->name != NULL; d++) {
        ms_module_def_native(vm, mod, d->name, d->fn, d->arity);
    }
}

void ms_module_export_value(MsVM* vm,
                            MsObjModule* mod,
                            const char* name,
                            MsValue value) {
    MsObjString* key = ms_obj_string_copy(vm, name, (int)strlen(name));
    ms_table_set(&mod->exports, key, value);
}
```

> **签名调整**：`MsBuiltinModuleInit` 已携带 `MsVM* vm`，模块 init 函数把 `vm` 透传给上面三个函数即可。

---

## 与 `ms_vm_define_native` 的关系

| 函数 | 写入目标 | 用途 |
|---|---|---|
| `ms_vm_define_native(vm, name, fn, arity)` | `vm->globals`（全局） | `print`、`type` 等语言核心全局 |
| `ms_module_def_native(vm, mod, name, fn, arity)` | `mod->exports`（模块命名空间） | 标准库模块函数 |

两者底层调用 `ms_obj_native_new`，行为相同；差异仅在写入目标。

---

## 模块 init 示例

```c
// src/stdlib/math.c

static MsValue ms_math_sqrt(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(sqrt(MS_AS_NUMBER(argv[0])));
}

/* 风格 A */
static const MsNativeDef ms_math_defs[] = {
    {"sqrt", ms_math_sqrt, 1},
    {"sin",  ms_math_sin,  1},
    {"cos",  ms_math_cos,  1},
    {NULL, NULL, 0}  // 哨兵
};

void ms_module_math_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_math_defs);
    ms_module_export_value(vm, mod, "PI",  MS_NUMBER_VAL(M_PI));
    ms_module_export_value(vm, mod, "INF", MS_NUMBER_VAL(INFINITY));
    ms_module_export_value(vm, mod, "NAN", MS_NUMBER_VAL(NAN));
}
```

```c
// 风格 B（等价）
void ms_module_math_init(MsVM* vm, MsObjModule* mod) {
    ms_module_def_native(vm, mod, "sqrt", ms_math_sqrt, 1);
    ms_module_def_native(vm, mod, "sin",  ms_math_sin,  1);
    ms_module_export_value(vm, mod, "PI", MS_NUMBER_VAL(M_PI));
}
```

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `include/ms/module.h` | 增 `MsNativeDef`、三个函数原型 |
| `src/module.c` | 实现 `ms_module_def_native`、`register_natives`、`export_value` |

---

## 测试要点

```c
// tests/unit/test_capi_native_def.c
// 1. 表式注册后，mod->exports 中能按名字查到对应 ObjNative
// 2. 哨兵正确终止：多注册一项 {NULL} 不崩溃
// 3. export_value 导出整数常量，.ms 端可读取
// 4. arity=-1 可变参：被调用时 argc 不受限
```
