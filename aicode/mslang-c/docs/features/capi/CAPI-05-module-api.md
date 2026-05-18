# CAPI-05: MsModuleApi 函数表

## 目标

向动态加载的扩展暴露一张**稳定的函数表**，扩展通过 `api->*` 调用而不直接 `#include "ms/object.h"`，使得：

1. 主进程不需要把内部结构体暴露在公共头文件里
2. ABI 版本号可以控制向后兼容

---

## 完整定义（`include/ms/module.h`）

```c
#define MS_MODULE_API_VERSION 1

typedef struct MsModuleApi {
    uint32_t version;   /* = MS_MODULE_API_VERSION */

    /* ── 注册 ─────────────────────────────────────────── */
    void (*def_native)(MsVM* vm, MsObjModule* mod,
                       const char* name, MsNativeFn fn, int arity);
    void (*register_natives)(MsVM* vm, MsObjModule* mod,
                             const MsNativeDef* defs);
    void (*export_value)(MsVM* vm, MsObjModule* mod,
                         const char* name, MsValue v);

    /* ── 值构造 ──────────────────────────────────────── */
    MsValue (*make_nil)(void);
    MsValue (*make_bool)(bool b);
    MsValue (*make_int)(int64_t i);
    MsValue (*make_number)(double d);
    MsValue (*make_string)(MsVM* vm, const char* s, int len);
    MsValue (*make_list)(MsVM* vm);
    MsValue (*make_map)(MsVM* vm);
    void    (*list_push)(MsVM* vm, MsValue list, MsValue v);
    void    (*map_set)(MsVM* vm, MsValue map, MsValue key, MsValue val);

    /* ── 错误与异步 ──────────────────────────────────── */
    MsValue (*raise)(MsVM* vm, const char* fmt, ...);
    MsValue (*make_future)(MsVM* vm);
    void    (*future_resolve)(MsVM* vm, MsValue fut, MsValue val);
    void    (*future_reject)(MsVM* vm, MsValue fut, MsValue err);

    /* ── 用户数据（扩展句柄类型）────────────────────── */
    MsValue (*userdata_new)(MsVM* vm, size_t bytes,
                            void (*finalize)(void* data),
                            void (*mark)(MsVM* vm, void* data),
                            const char* type_tag);
    void*       (*userdata_data)(MsValue v);
    const char* (*userdata_tag)(MsValue v);
    bool        (*userdata_is)(MsValue v, const char* tag);

    /* ── 值解包（扩展只读取，不修改内部结构）────────── */
    bool    (*is_nil)(MsValue v);
    bool    (*is_bool)(MsValue v);
    bool    (*is_int)(MsValue v);
    bool    (*is_number)(MsValue v);
    bool    (*is_string)(MsValue v);
    bool    (*val_to_bool)(MsValue v);
    int64_t (*val_to_int)(MsValue v);
    double  (*val_to_number)(MsValue v);
    const char* (*val_to_cstring)(MsValue v);  /* 指向内部 chars，只读 */
    int         (*string_len)(MsValue v);

    /* ── 容器/反射（只读）──────────────────────────────── */
    bool    (*is_list)(MsValue v);
    bool    (*is_map)(MsValue v);
    bool    (*is_tuple)(MsValue v);
    bool    (*is_function)(MsValue v);
    bool    (*is_userdata)(MsValue v, const char* tag);
    int     (*list_len)(MsValue v);
    MsValue (*list_get)(MsValue v, int idx);
    bool    (*map_get)(MsValue v, MsValue key, MsValue* out);
} MsModuleApi;
```

---

## 单例实现（`src/module_api.c`）

```c
#include "ms/module.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/vm.h"
#include <stdarg.h>

static MsValue api_make_nil(void)                      { return MS_NIL_VAL(); }
static MsValue api_make_bool(bool b)                   { return MS_BOOL_VAL(b); }
static MsValue api_make_int(int64_t i)                 { return MS_INT_VAL(i); }
static MsValue api_make_number(double d)               { return MS_NUMBER_VAL(d); }
static MsValue api_make_string(MsVM* vm, const char* s, int len) {
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s, len));
}
static MsValue api_make_list(MsVM* vm)  { return MS_OBJ_VAL(ms_obj_list_new(vm)); }
static MsValue api_make_map(MsVM* vm)   { return MS_OBJ_VAL(ms_obj_map_new(vm)); }
// ... 其他 static 实现 ...

static MsValue api_raise(MsVM* vm, const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    ms_vm_runtime_error(vm, "%s", buf);
    return MS_NIL_VAL();
}

static const MsModuleApi k_api = {
    .version          = MS_MODULE_API_VERSION,
    .def_native       = ms_module_def_native,
    .register_natives = ms_module_register_natives,
    .export_value     = ms_module_export_value,
    .make_nil         = api_make_nil,
    .make_bool        = api_make_bool,
    .make_int         = api_make_int,
    .make_number      = api_make_number,
    .make_string      = api_make_string,
    .make_list        = api_make_list,
    .make_map         = api_make_map,
    // ... 其余字段 ...
    .raise            = api_raise,
};

const MsModuleApi* ms_module_api_get(void) { return &k_api; }
```

`include/ms/module.h` 声明：

```c
const MsModuleApi* ms_module_api_get(void);
```

---

## Exception protocol

`api->raise` 内部调用 `ms_vm_runtime_error`；调用后扩展函数**必须立即 `return MS_NIL_VAL()`**，VM 在控制权返回时检查错误状态并展开调用栈。**不要**在 raise 之后继续访问 MsValue / MsObject。

```c
// 正确用法
if (argc < 1) return api->raise(vm, "expected 1 argument, got 0");
// 只有未 raise 时才执行到这里
```

若需在函数中提前返回并检查是否已出错，可使用：
```c
bool (*has_error)(MsVM* vm);  /* 检查 vm->had_runtime_error */
```
（此字段在 `MsModuleApi.version >= 2` 中加入；v1 扩展直接 return 即可。）

---

## MsObjUserdata（`include/ms/object.h`）

扩展用 userdata 实现私有句柄（无需新增 `MS_OBJ_*` 枚举值）：

```c
typedef struct {
    MsObject obj;           /* type = MS_OBJ_USERDATA */
    void*    data;          /* 扩展自己 malloc 的数据，finalize 时释放 */
    size_t   data_size;     /* 调试/序列化用，可选 */
    void   (*finalize)(void* data);          /* GC 回收时调用 */
    void   (*mark)(MsVM* vm, void* data);    /* GC mark 阶段扫描内部引用 */
    const char* type_tag;   /* "MsHasher"、"MsLogger"...，用于运行时类型检查 */
} MsObjUserdata;
```

```c
MsValue api_userdata_new(MsVM* vm, size_t bytes,
                         void (*fin)(void*),
                         void (*mark)(MsVM*, void*),
                         const char* tag) {
    MsObjUserdata* ud = (MsObjUserdata*)ms_allocate_object(
        vm, sizeof(MsObjUserdata), MS_OBJ_USERDATA);
    ud->data      = bytes > 0 ? malloc(bytes) : NULL;
    ud->data_size = bytes;
    ud->finalize  = fin;
    ud->mark      = mark;
    ud->type_tag  = tag;  /* 调用方保证生命周期 */
    return MS_OBJ_VAL(ud);
}
```

GC：`ms_vm_gc.c` 中 `MS_OBJ_USERDATA` 分支调 `ud->mark(vm, ud->data)`（若非 NULL）；free 时调 `ud->finalize(ud->data)` + `free(ud->data)`。

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `include/ms/module.h` | 增 `MsModuleApi`、`MS_MODULE_API_VERSION`、`ms_module_api_get` 声明 |
| `include/ms/object.h` | 增 `MS_OBJ_USERDATA`、`MsObjUserdata` 结构 |
| `src/module_api.c` | 新建；k_api 单例及所有 api_* 实现 |
| `src/object.c` | `MS_OBJ_USERDATA` 的 print/free |
| `src/vm_gc.c` | `MS_OBJ_USERDATA` 的 mark/finalize |
| `CMakeLists.txt` | 加 `src/module_api.c` |
