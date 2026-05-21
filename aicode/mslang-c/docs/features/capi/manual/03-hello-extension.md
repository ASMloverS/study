# 03 — Hello 扩展教程

本章走通写一个最小 C 扩展的完整路径，对应示例工程 `examples/00-hello/`。

---

## 目标

写一个名为 `hello` 的动态扩展，让 `.ms` 脚本能够：

```ms
import hello
print(hello.hello("World"))   # Hello, World!
print(hello.VERSION)           # 1
```

---

## 1. 入口函数签名

每个动态扩展必须导出一个名为 `ms_module_init` 的函数：

```c
MS_EXPORT void ms_module_init(const MsModuleApi* api,
                               MsVM*              vm,
                               MsObjModule*       mod);
```

- **`MS_EXPORT`**：平台宏，定义于 `ms/common.h`（通过 `ms/module.h` 自动引入）。
  - Linux/macOS → `__attribute__((visibility("default")))`
  - Windows → `__declspec(dllexport)`
- **`api`**：VM 提供的版本化函数表，扩展通过 `api->*` 与 VM 交互，不直接 `#include` 内部头文件。
- **`mod`**：当前正在初始化的模块对象，用于向 `.ms` 侧挂载函数和值。

---

## 2. hello.c 逐行解析

```c
#include "ms/module.h"   /* MsModuleApi, MS_EXPORT, MsNativeFn, … */
#include <stdio.h>

/* 将 api 保存到模块级静态变量，让 native 函数能访问它。 */
static const MsModuleApi* g_api;

/* native 函数签名：vm + 参数个数 + 参数数组 */
static MsValue hello_greet(MsVM* vm, int argc, MsValue* argv) {
    /* 1. 校验参数 */
    if (argc < 1 || !g_api->is_string(argv[0])) {
        return g_api->raise(vm, "hello(name): expected string");
        /* raise 后必须立即 return；raise 返回的 MsValue 是哨兵，不可继续访问 */
    }

    /* 2. 提取字符串（指针有效直到下一次触发分配的 api 调用） */
    const char* name = g_api->val_to_cstring(argv[0]);

    /* 3. 在本地缓冲区拼接结果 */
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "Hello, %s!", name);

    /* 4. 构造 VM 字符串并返回 */
    return g_api->make_string(vm, buf, len);
}

MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    /* ABI 版本协商：低于要求版本时拒绝加载 */
    if (api->version < 1) {
        api->raise(vm, "hello: requires MsModuleApi v1");
        return;
    }
    g_api = api;

    /* 注册 native 函数：名称 / 函数指针 / 形参个数 */
    api->def_native(vm, mod, "hello", hello_greet, 1);

    /* 挂载任意 MsValue（常量、Userdata 等）—— 与 def_native 的区别 */
    api->export_value(vm, mod, "VERSION", api->make_int(1));
}
```

### `g_api` 静态变量

Native 函数收到的参数是 `(vm, argc, argv)`，没有 `api` 指针。标准做法是在 `ms_module_init` 中把 `api` 存入模块级静态变量，供所有 native 函数使用。每个动态库有独立的数据段，不同扩展之间的 `g_api` 互不干扰。

### `export_value` 与 `def_native`

| | `def_native` | `export_value` |
|---|---|---|
| 挂载类型 | 仅 native 函数 | 任意 `MsValue`（整数、字符串、Userdata …）|
| `.ms` 访问 | `mod.fn(...)` | `mod.name` |

`VERSION` 是整数常量，使用 `export_value` + `api->make_int(1)`。

---

## 3. CMakeLists.txt

```cmake
add_library(hello SHARED hello.c)
target_include_directories(hello PRIVATE ${MSLANG_INCLUDE_DIR})
```

- `SHARED`：生成共享库（`.so` / `.dll` / `.dylib`）。
- `MSLANG_INCLUDE_DIR`：由顶层 `examples/CMakeLists.txt` 设置，指向 `include/`。
- 扩展**不**链接 mslang-c 主程序，所有 VM 功能通过 `api->*` 指针调用。

---

## 4. 编译

从项目根目录执行：

```bash
cmake -S docs/features/capi/manual/examples \
      -B docs/features/capi/manual/examples/build
cmake --build docs/features/capi/manual/examples/build --config Release
```

产物（平台相关）：

| 平台 | 产物文件 |
|---|---|
| Linux | `build/00-hello/libhello.so` |
| macOS | `build/00-hello/libhello.dylib` |
| Windows | `build/00-hello/hello.dll` |

---

## 5. 运行

把构建产物目录加入 `MSLANG_PATH`，再运行脚本：

```bash
MSLANG_PATH=docs/features/capi/manual/examples/build/00-hello \
  ./build/mslang-c docs/features/capi/manual/examples/00-hello/run.ms
```

预期输出：

```
Hello, World!
1
```

Windows（PowerShell，MSVC 多配置构建输出在 `Release\` 子目录）：

```powershell
$env:MSLANG_PATH = "docs\features\capi\manual\examples\build\00-hello\Release"
.\build\mslang-c.exe docs\features\capi\manual\examples\00-hello\run.ms
```

---

## 6. `export_value` 与全局符号（CAPI-08）

`ms_module_init` 内通过 `api->export_value(mod, "name", val)` 将任意 `MsValue` 挂到模块作用域。`.ms` 侧 `import mod; mod.name` 即可访问。

详见 [CAPI-08-globals-migration.md](../CAPI-08-globals-migration.md)。

---

## 进一步阅读

- **动态加载 ABI**：[CAPI-04-dynamic-loading.md](../CAPI-04-dynamic-loading.md)
- **MsModuleApi 完整定义**：[CAPI-05-module-api.md](../CAPI-05-module-api.md)
- **值构造与错误处理**：[04-values-and-errors.md](04-values-and-errors.md)
- **打包与分发**：[09-packaging-and-distribution.md](09-packaging-and-distribution.md)
