# 09 — 打包与分发

本章说明如何将动态扩展打包成可分发形式，涵盖 CMake 模板、头文件依赖、
ABI 版本协商和跨平台注意事项。内置 stdlib 模块作者请参阅第 08 章。

---

## 1. 最小头文件集

动态扩展**不需要** mslang-c 完整源码，只需三个公开头：

| 头文件 | 提供内容 |
|---|---|
| `ms/common.h` | 平台宏（`MS_EXPORT`、整型别名、`static_assert`） |
| `ms/value.h` | `MsValue` 定义、`MS_IS_*`/`MS_AS_*` 宏 |
| `ms/module.h` | `MsModuleApi`、`MsNativeFn`、`MsNativeDef`、`ms_module_init` 签名 |

这三个文件之间存在包含依赖（`module.h` → `value.h` → `common.h`），只需在扩展中 `#include "ms/module.h"` 即可。

---

## 2. CMake 模板

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_ext C)

# 头文件路径：同仓库时用相对路径；独立分发时由用户通过 -DMSLANG_INCLUDE_DIR 指定
if(NOT DEFINED MSLANG_INCLUDE_DIR)
    set(MSLANG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../include")
endif()

add_library(my_ext SHARED my_ext.c)

target_include_directories(my_ext PRIVATE "${MSLANG_INCLUDE_DIR}")

# 不链接主程序：所有 VM 功能通过 api->* 指针调用
# target_link_libraries(my_ext PRIVATE mslang-c)  ← 禁止

# 可选：隐藏非导出符号，与 MS_EXPORT 配合减小符号表
if(NOT WIN32)
    target_compile_options(my_ext PRIVATE -fvisibility=hidden)
endif()
```

独立分发时用户只需：

```bash
cmake -S . -B build -DMSLANG_INCLUDE_DIR=/path/to/mslang-c/include
cmake --build build
```

---

## 3. ABI 版本协商

在 `ms_module_init` 开头检查 `api->version`，版本不满足时通过 `raise` 拒绝加载：

```c
MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    /* 拒绝低版本：raise 后 VM 将 import 标为失败，向 .ms 端抛错 */
    if (api->version < 1) {
        api->raise(vm, "my_ext: requires MsModuleApi v1, got v%d", api->version);
        return;
        /* ↑ 不 raise 直接 return → 空模块加载"成功"，调试极困难，避免 */
    }

    /* 检测可选 v2 字段（向前兼容）*/
    if (api->version >= 2 && api->threadpool_submit) {
        /* 走真异步路径（v2 特性） */
    }

    /* v1 通用初始化 */
    g_api = api;
    api->def_native(vm, mod, "foo", fn_foo, 1);
}
```

`.ms` 端捕获加载失败：

```ms
import "my_ext"   # 若版本不匹配，此处抛出 ImportError
```

---

## 4. 跨平台产物

| 平台 | 产物文件 | 加载方式 |
|---|---|---|
| Linux | `libmy_ext.so` | `dlopen` |
| macOS | `libmy_ext.dylib` | `dlopen` |
| Windows | `my_ext.dll` | `LoadLibraryW` |

### Linux — rpath

将扩展放在非标准目录时，设置 `RPATH` 避免运行时找不到依赖：

```cmake
set_target_properties(my_ext PROPERTIES
    INSTALL_RPATH "$ORIGIN"   # 与 .so 同目录查找依赖
    BUILD_RPATH   "$ORIGIN")
```

### macOS — install_name

```cmake
set_target_properties(my_ext PROPERTIES
    MACOSX_RPATH ON
    INSTALL_NAME_DIR "@rpath")
```

### Windows — PDB 旁挂

调试符号文件 `my_ext.pdb` 应与 `my_ext.dll` 放在同一目录，便于崩溃分析：

```cmake
if(MSVC)
    target_compile_options(my_ext PRIVATE /Zi)
    target_link_options(my_ext PRIVATE /DEBUG /PDB:$<TARGET_PDB_FILE:my_ext>)
endif()
```

---

## 5. 校验清单

扩展完成后按以下顺序校验：

```bash
# 1. 编译
cmake -S . -B build && cmake --build build

# 2. 复制到 MSLANG_PATH 可见目录
cp build/libmy_ext.so /usr/local/lib/mslang/

# 3. 基本 import 测试
MSLANG_PATH=/usr/local/lib/mslang \
  mslang-c -e 'import "my_ext"; print(my_ext.foo(42))'

# 4. 运行完整脚本
MSLANG_PATH=/usr/local/lib/mslang \
  mslang-c run.ms
```

Windows（PowerShell）：

```powershell
cmake -S . -B build
cmake --build build --config Release
$env:MSLANG_PATH = "build\Release"
.\mslang-c.exe run.ms
```

**调试 import 失败**：设置环境变量 `MSLANG_TRACE_IMPORT=1`，VM 会打印三段查找（注册表 → 文件系统 → 动态库）的每步细节及最终 `dlopen` 错误。

---

## 进一步阅读

- **动态加载 ABI**：[CAPI-04-dynamic-loading.md](../CAPI-04-dynamic-loading.md)
- **MsModuleApi 版本字段**：[CAPI-05-module-api.md](../CAPI-05-module-api.md)
- **模块查找路径**：[CAPI-03-search-path.md](../CAPI-03-search-path.md)
- **内置模块作者指南**：[08-builtin-module-author.md](08-builtin-module-author.md)
- **Hello 扩展教程**：[03-hello-extension.md](03-hello-extension.md)
