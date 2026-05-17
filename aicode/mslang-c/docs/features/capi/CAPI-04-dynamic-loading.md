# CAPI-04: 动态加载（.dll / .so / .dylib）

## 目标

`import mymod` 在内置注册表和文件系统均未命中时，按 `vm->module_search_paths` 查找动态库，`dlopen` / `LoadLibrary` 后调用导出符号 `ms_module_init` 完成模块填充。

---

## `MS_EXPORT` 宏（`include/ms/common.h`）

```c
#ifdef _WIN32
#  define MS_EXPORT __declspec(dllexport)
#else
#  define MS_EXPORT __attribute__((visibility("default")))
#endif
```

---

## 扩展作者编写的 `ms_module_init`

```c
// my_ext/sample.c  — 编译为 libsample.so / sample.dll
#include "ms/module.h"

static MsValue hello(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    return MS_OBJ_VAL(ms_obj_string_copy(vm, "hello from ext", 14));
}

MS_EXPORT void ms_module_init(const MsModuleApi* api,
                               MsVM*             vm,
                               MsObjModule*      mod) {
    /* 版本检查（可选，v1 主进程不强制） */
    if (api->version < 1) return;

    api->def_native(vm, mod, "hello", hello, 0);
}
```

---

## `dynlib.h` — 跨平台薄封装（`include/ms/dynlib.h`）

```c
typedef void* MsDynlib;

MsDynlib  ms_dynlib_open(const char* path);
void*     ms_dynlib_sym(MsDynlib lib, const char* name);
void      ms_dynlib_close(MsDynlib lib);
```

实现（`src/dynlib.c`）：

```c
#ifdef _WIN32
#  include <windows.h>
MsDynlib  ms_dynlib_open(const char* p) { return (MsDynlib)LoadLibraryA(p); }
void*     ms_dynlib_sym(MsDynlib l, const char* n) { return (void*)GetProcAddress((HMODULE)l, n); }
void      ms_dynlib_close(MsDynlib l) { FreeLibrary((HMODULE)l); }
#else
#  include <dlfcn.h>
MsDynlib  ms_dynlib_open(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
void*     ms_dynlib_sym(MsDynlib l, const char* n) { return dlsym(l, n); }
void      ms_dynlib_close(MsDynlib l) { dlclose(l); }
#endif
```

---

## 库名解析（`src/module_loader.c`）

```c
typedef void (*MsModuleInitFn)(const MsModuleApi*, MsVM*, MsObjModule*);

/* 按搜索路径逐一尝试 lib{name}.so / {name}.dll / lib{name}.dylib */
static MsDynlib find_dynlib(MsVM* vm, const char* name, char* out_path) {
    for (int i = 0; i < vm->module_search_count; i++) {
        const char* dir = vm->module_search_paths[i];
        /* 拼装候选路径 */
#ifdef _WIN32
        snprintf(out_path, MS_PATH_MAX, "%s/%s.dll", dir, name);
#elif defined(__APPLE__)
        snprintf(out_path, MS_PATH_MAX, "%s/lib%s.dylib", dir, name);
#else
        snprintf(out_path, MS_PATH_MAX, "%s/lib%s.so", dir, name);
#endif
        MsDynlib lib = ms_dynlib_open(out_path);
        if (lib) return lib;
    }
    return NULL;
}
```

---

## 动态加载流程（插入 `ms_module_load` 步骤 4）

```c
/* 步骤 4 — 动态加载 */
char lib_path[MS_PATH_MAX];
MsDynlib lib = find_dynlib(vm, module_name, lib_path);
if (lib) {
    MsModuleInitFn init_fn =
        (MsModuleInitFn)ms_dynlib_sym(lib, "ms_module_init");
    if (!init_fn) { ms_dynlib_close(lib); /* 找不到符号 */ return NULL; }

    /* 合成路径用 lib_path，保证 cache key 唯一 */
    MsObjString* key = ms_obj_string_copy(vm, lib_path, (int)strlen(lib_path));
    MsObjString* mod_name = ms_obj_string_copy(vm, module_name,
                                                (int)strlen(module_name));
    MsObjModule* mod = ms_obj_module_new(vm, mod_name, key);
    mod->state = MS_MOD_INITIALIZING;
    ms_table_set(&vm->module_cache, key, MS_OBJ_VAL(mod));

    /* 传入 MsModuleApi 实例（CAPI-05 定义）*/
    const MsModuleApi* api = ms_module_api_get();
    init_fn(api, vm, mod);

    mod->state = MS_MOD_INITIALIZED;
    ms_vm_track_dynlib(vm, lib);  // 记录句柄，vm_free 时 dlclose
    return mod;
}
return NULL;  // 未找到
```

---

## 句柄生命周期

`MsVM` 新增：

```c
MsDynlib* dynlib_handles;
int       dynlib_count;
int       dynlib_cap;
```

`ms_vm_free` 遍历并 `ms_dynlib_close` 全部句柄，避免测试套件中的句柄泄漏。

---

## ABI 版本

`MsModuleApi.version = MS_MODULE_API_VERSION`（首版 = 1）。

v1 主进程不强制校验版本；扩展侧读取后决定降级行为。当 API 发生不兼容变更时递增此值。

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `include/ms/common.h` | 增 `MS_EXPORT` 宏 |
| `include/ms/dynlib.h` | 新建；`MsDynlib`、三个函数 |
| `include/ms/vm.h` | `MsVM` 增 `dynlib_handles`/count/cap |
| `src/dynlib.c` | 新建；跨平台实现 |
| `src/module_loader.c` | `find_dynlib`、`ms_vm_track_dynlib` |
| `src/module.c` | `ms_module_load` 插入步骤 4 |
| `src/vm.c` | `ms_vm_free` 关闭 dynlib 句柄 |
| `CMakeLists.txt` | 链接 `dl`（Unix）；无需 Windows 额外链接 |

---

## 样例扩展（`tests/fixtures/stdlib_ext/`）

```
tests/fixtures/stdlib_ext/
    sample_ext.c       # 实现 hello() 函数
    CMakeLists.txt     # 独立 add_library(sample_ext SHARED sample_ext.c)
```

`tests/unit/test_capi_dynamic.c`：
```c
// 编译阶段知道 sample_ext 产物路径，ms_vm_add_search_path 指向它，
// 执行 "import sample_ext; sample_ext.hello()" 断言返回 "hello from ext"
```

---

## v1 不做

- 版本协商（字段已占位）
- 符号前缀命名空间隔离
- 热重载（reload）
