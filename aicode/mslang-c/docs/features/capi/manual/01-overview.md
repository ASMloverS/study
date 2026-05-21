# 01 — CAPI 概述

## CAPI 是什么

CAPI（C Extension API）是 mslang-c 向第三方提供的**稳定 ABI 接口**，让 C 代码以动态库形式扩展 VM 功能。核心载体是 `MsModuleApi` 函数表：扩展通过 `api->make_string(…)`、`api->raise(…)` 等指针调用 VM 功能，而不直接 `#include` VM 内部头文件。

**CAPI 不是什么：**
- 不是 VM 内部实现文档（内部结构见 `include/ms/object.h`，不保证稳定）
- 不是脚本语言规格（`.ms` 语法见语言手册）
- 不是内置 stdlib 的全部能力（内置模块可 `#include` 内部头，第三方扩展不行）

---

## 读者地图

根据你的角色，按下表选择阅读路径：

| 章节 | `.ms` 脚本用户 | 第三方扩展作者 | 内置 stdlib 作者 |
|---|:---:|:---:|:---:|
| 01 概述（本章）| √ | √ | √ |
| 02 使用模块 | √ | ○ | — |
| 03 Hello 扩展教程 | — | √ | ○ |
| 04 值与错误处理 | — | √ | √ |
| 05 Userdata 句柄 | — | √ | √ |
| 06 异步扩展 | — | √ | √ |
| 07 字节流互操（v1）| — | √ | ○ |
| 08 内置模块作者指南 | — | — | √ |
| 09 打包与分发 | — | √ | — |
| 10 MsModuleApi 参考 | — | √ | √ |

> √ 必读 · ○ 可选 · — 通常可跳过

---

## 词汇表

### Module

`.ms` 脚本可通过 `import` 引入的命名单元。内置模块（`io`、`net`）在启动时注册到内置注册表；动态模块是文件系统上的共享库（`.so` / `.dll` / `.dylib`）。

```ms
import io
io.open("hello.txt", "r").read()
```

### Native

用 C 实现并注册到 `MsModuleApi` 函数表的函数。`.ms` 侧调用 `mod.func(...)` 时，VM 直接跳转到对应的 `MsNativeFn` 指针。

```c
// 注册一个返回整数的 native
api->def_native(vm, module, "add", my_add_fn, 2);
```

### Userdata

用于将任意 C 句柄（文件描述符、哈希上下文、网络连接等）包装为 VM 可管理的 GC 对象，而无需新增 `MS_OBJ_*` 枚举值。VM 在回收时调用你提供的 `finalize` 回调。

```c
// 分配 64 字节私有数据，GC 时调用 my_close
MsValue h = api->userdata_new(vm, 64, my_close, NULL, "mylib.Handle");
```

### Future

异步操作的占位值：`make_future` 创建后，worker 线程完成任务再调用 `future_resolve` 或 `future_reject`，`.ms` 侧可用 `await` 等待结果。

```c
MsValue fut = api->make_future(vm);
// … worker 线程完成后 …
api->future_resolve(vm, fut, api->make_string(vm, result, len));
```

> **v1 约束**：`MsModuleApi v1` 不导出 VM 线程池接口；`future_resolve` 须在调用 native 的同一线程（或外部同步）调用。详见第 06 章。

### ObjFile / ObjBuffer

VM 内置的文件对象和字节缓冲区对象，分别对应 `MS_OBJ_FILE` 和 `MS_OBJ_BUFFER` 枚举。

- **内置模块作者**（可 `#include "ms/stdlib/objbuffer.h"`）可直接构造并返回这两种类型。
- **第三方扩展作者**在 `MsModuleApi v1` 下无法直接操作，需用 `ObjString` 字节流替代（见第 07 章）。

### MsModuleApi

扩展与 VM 之间的版本化函数表接口，定义于 `include/ms/module.h`：

```c
typedef struct MsModuleApi {
    uint32_t version;   /* 当前 = 1 */
    /* 注册 / 值构造 / 错误与异步 / Userdata / 值解包 … */
} MsModuleApi;
```

`ms_module_init` 在动态库加载时被调用，`api` 指针指向此表。版本协商：

```c
void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "requires MsModuleApi v1");
        return;
    }
    /* … */
}
```

---

## 进一步阅读

- **如何在 `.ms` 中使用模块**：[02-using-modules.md](02-using-modules.md)
- **MsModuleApi 完整定义**：[CAPI-05-module-api.md](../CAPI-05-module-api.md)
- **动态加载 ABI**：[CAPI-04-dynamic-loading.md](../CAPI-04-dynamic-loading.md)
- **从第一个扩展开始**：[03-hello-extension.md](03-hello-extension.md)
