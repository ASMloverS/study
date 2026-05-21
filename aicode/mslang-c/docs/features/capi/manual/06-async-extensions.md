# 06 — 异步扩展

本章讲解 mslang 的 Future 模型，说明 `MsModuleApi v1` 的异步约束，
并通过 `examples/03-async-fileread/` 演示同步等价实现。

---

## 1. Future 模型

mslang 的异步协作建立在 `ObjFuture` 上：`.ms` 侧 `await` 表达式挂起当前协程，
等待 VM 将 Future 标记为 resolved 或 rejected。内置异步 IO（`io.read_file_async`）
完整路径如下：

```
ms_io_read_file_async（内置 native）
    ├─ make_future(vm)           ← 分配 ObjFuture，返回 MsValue
    ├─ vm_pin_future(fut)        ← 加入 GC root，防止 await 前被回收
    ├─ threadpool_submit(job)    ← 把阻塞 IO 推给 worker 线程
    └─ 返回 MsValue(fut)         ← .ms 侧: result = await io.read_file_async(path)

Worker 线程（不触碰 MsValue / MsObject）
    └─ fread → result_buf → push done_queue → wakeup 主线程

主线程 EventLoop（下一轮 poll）
    └─ on_wakeup → future_resolve(vm, fut, str_val) → 协程继续
```

**关键约束**：worker 线程只处理原始 C 数据（`char*`、`size_t`）；
任何 `MsValue` / `MsObject` 的读写都只能在主线程发生。
`future_resolve` / `future_reject` 只能从主线程（EventLoop 回调中）调用。

---

## 2. v1 约束

`MsModuleApi v1` 函数表不包含异步相关字段：
`make_future`、`future_resolve`、`future_reject`、`threadpool_submit` 均未暴露。

这意味着：**v1 扩展无法创建 Future，也无法接入 VM 的线程池**。
需要 IO 的 v1 扩展有两种选择：

| 选择 | 说明 | 适用场景 |
|---|---|---|
| **同步阻塞** | 在 native 函数内直接执行 IO，阻塞主线程 | 小文件、原型、工具脚本 |
| **扩展自管线程** | 自行创建线程，通过共享状态返回结果 | 需要并发时，但线程安全由扩展自己保证 |

本章示例采用第一种——同步阻塞，是 v1 下最简单、最安全的路径。

---

## 3. 同步等价实现：`examples/03-async-fileread/`

扩展名为 `fileread`，暴露一个函数：

```ms
import "fileread"

contents = fileread.read_sync("path/to/file.txt")
print(contents)
```

### 3.1 C 实现

```c
#include "ms/module.h"
#include <stdio.h>
#include <stdlib.h>

static const MsModuleApi* g_api;

/* fileread.read_sync(path) -> string
   同步读取文件，阻塞主线程直到读完。

   v2 注：MsModuleApi v2 将暴露 make_future / future_resolve /
   threadpool_submit，届时可改写为非阻塞异步版本（见第 4 节）。 */
static MsValue fn_read_sync(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1)
        return g_api->raise(vm, "fileread.read_sync(path): expected 1 argument");
    if (!g_api->is_string(argv[0]))
        return g_api->raise(vm, "fileread.read_sync: path must be a string");

    /* val_to_cstring 指针有效期：直到「下一次调用任何触发分配的 api->* 函数」。
       make_string 是此函数内第一个触发分配的 api 调用，因此 path 在此之前安全。
       fopen / malloc / fread 均是普通 C 调用，不触发 VM 分配。 */
    const char* path = g_api->val_to_cstring(argv[0]);

    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f)
        return g_api->raise(vm, "fileread.read_sync: cannot open file");

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return g_api->raise(vm, "fileread.read_sync: out of memory");
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    /* make_string 将 buf 拷贝进 VM 管理的 ObjString；此后 path 指针失效，但已不再使用。 */
    MsValue result = g_api->make_string(vm, buf, (int)n);
    free(buf);
    return result;
}

MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "fileread: requires MsModuleApi v1");
        return;
    }
    g_api = api;
    api->def_native(vm, mod, "read_sync", fn_read_sync, 1);
}
```

### 3.2 `.ms` 调用端（`run.ms`）

```ms
import "fileread"

contents = fileread.read_sync(
    "docs/features/capi/manual/examples/03-async-fileread/sample.txt"
)
print(contents)
```

路径相对于运行命令时的工作目录（项目根目录 `mslang-c/`）。

---

## 4. v2 路线（不承诺）

`MsModuleApi v2` 计划新增以下字段：

```c
/* v2 新增（示意，不在 v1 struct 中） */
MsValue (*make_future)(MsVM* vm);
void    (*future_resolve)(MsVM* vm, MsValue fut, MsValue result);
void    (*future_reject)(MsVM* vm, MsValue fut, MsValue error);
void    (*threadpool_submit)(MsVM* vm, MsJob* job);
```

届时，扩展可在 `ms_module_init` 检测版本并按能力选择路径：

```c
MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "fileread: requires MsModuleApi v1");
        return;
    }
    g_api = api;

    if (api->version >= 2) {
        /* v2: 注册真异步版本 */
        api->def_native(vm, mod, "read_async", fn_read_async_v2, 1);
    }
    /* v1 始终可用的同步版本 */
    api->def_native(vm, mod, "read_sync", fn_read_sync, 1);
}
```

v2 异步约束（与内置模块相同）：
- `make_future` 在主线程调用（native 函数执行期间即主线程）
- `threadpool_submit` 后主线程继续 EventLoop，不等待 worker
- worker 线程**不得**调用任何 `api->*` 函数，只操作原始 C 数据
- `future_resolve` / `future_reject` 只能在主线程的 EventLoop 回调中调用

---

## 5. 运行示例

```bash
cmake -S docs/features/capi/manual/examples \
      -B docs/features/capi/manual/examples/build
cmake --build docs/features/capi/manual/examples/build --config Release

MSLANG_PATH=docs/features/capi/manual/examples/build/03-async-fileread/Release \
  ./build/mslang-c docs/features/capi/manual/examples/03-async-fileread/run.ms
```

预期输出：

```
Hello from fileread!
This is the synchronous file-read example.
```

Windows（PowerShell）：

```powershell
$env:MSLANG_PATH = "docs\features\capi\manual\examples\build\03-async-fileread\Release"
.\build\mslang-c.exe docs\features\capi\manual\examples\03-async-fileread\run.ms
```

---

## 进一步阅读

- **值与错误处理**（`val_to_cstring` 生命周期）：[04-values-and-errors.md](04-values-and-errors.md)
- **Userdata 句柄**：[05-userdata-handles.md](05-userdata-handles.md)
- **线程池设计规格**：[CAPI-07-threadpool.md](../CAPI-07-threadpool.md)
- **字节流互操**：[07-bytes-interop.md](07-bytes-interop.md)
