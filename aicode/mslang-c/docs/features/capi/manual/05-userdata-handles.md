# 05 — Userdata 句柄

本章讲解如何用 `userdata_new` 将 C 句柄包装为 VM 可管理的 GC 对象，
对应示例工程 `examples/02-userdata-hash/`，扩展名为 `djb2`。

> **为什么叫 djb2？** 标准库已占用裸名 `hash`（stub 模块）；
> 动态扩展选用算法名 `djb2` 以避免冲突，符合真实项目的命名习惯。

---

## 目标

写一个名为 `djb2` 的扩展，包装一个简单的哈希累积器句柄：

```ms
import "djb2"

h = djb2.create()
djb2.update(h, "hello")
djb2.update(h, " ")
djb2.update(h, "world")
print(djb2.digest(h))     // -4677384763180952763

djb2.reset(h)
djb2.update(h, "hello world")
print(djb2.digest(h))     // -4677384763180952763（reset + 整串 = 相同）

h2 = djb2.create()
djb2.update(h2, "abc")
print(djb2.digest(h2))    // 193409669
```

---

## 1. 为什么需要 Userdata

将 C 句柄传递给 `.ms` 脚本有两种路径：

| 路径 | 说明 | 适用场景 |
|---|---|---|
| 新增 `MS_OBJ_*` 枚举 | 修改 VM 内部，需要改 object.h / vm_gc.c 等 | 内置 stdlib 作者 |
| **Userdata** | 不改 VM；通过 `api->userdata_new` 包装任意 C 指针 | **第三方扩展作者** |

Userdata 在 `.ms` 侧是不透明的——脚本不能访问其内部字段，只能把它传给同一扩展的其他函数，通过 `userdata_is` 验证身份后操作。

---

## 2. 私有载荷结构

```c
/* 存储在 VM 分配的数据缓冲区中的私有状态 */
typedef struct {
    uint64_t state;
} HashCtx;
```

`userdata_new(vm, sizeof(HashCtx), ...)` 让 VM 用自己的 `malloc` 分配
`sizeof(HashCtx)` 字节并存入 `ud->data`。GC 在回收时自动释放该缓冲区。

---

## 3. 创建 Userdata：`fn_create`

```c
static const char* const kHashTag = "djb2.Hash";

static MsValue fn_create(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    /* userdata_new(vm, bytes, finalize, mark, type_tag)
       finalize=NULL: HashCtx 无外部资源，VM 自动释放 ud->data
       mark=NULL:     HashCtx 不持有任何 MsValue 引用 */
    MsValue ud = g_api->userdata_new(vm, sizeof(HashCtx),
                                     NULL, NULL, kHashTag);
    HashCtx* ctx = (HashCtx*)g_api->userdata_data(ud);
    ctx->state = 5381; /* djb2 初始种子 */
    return ud;
}
```

### `finalize` 与 `mark` 参数

| 参数 | 何时传非 NULL | 典型用途 |
|---|---|---|
| `finalize` | 句柄持有外部资源（fd、锁、私有堆内存）| `fclose(fd)` / `free(private_buf)` |
| `mark` | 句柄内部存储了 `MsValue` 引用 | 调用 `api->mark_value(v)` 防止引用被 GC 回收 |

**重要**：`finalize` 在 GC 临界区被调用，**禁止在其中调用任何 `api->*` 函数**；
VM 会在 `finalize` 返回后自动释放 `ud->data` 缓冲区，无需在 `finalize` 内 `free(data)`。

### `type_tag` 参数

`type_tag` 必须是静态生存期的字符串字面量（例如模块前缀 + 类型名）。
`userdata_is` 内部先走指针相等（fast path），失败时 fallback 到 `strcmp`；
跨 DLL 边界时 `strcmp` 是唯一可靠的比较方式。

---

## 4. 操作 Userdata：类型检查与数据访问

`fn_update` 演示完整的防御校验流程：

```c
static MsValue fn_update(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2)
        return g_api->raise(vm, "djb2.update(h, data): expected 2 arguments");
    /* 1. 用 is_userdata 验证类型，防止调用方传入错误句柄 */
    if (!g_api->is_userdata(argv[0], kHashTag))
        return g_api->raise(vm, "djb2.update: h must be a djb2.Hash handle");
    /* 2. 校验第二个参数为字符串 */
    if (!g_api->is_string(argv[1]))
        return g_api->raise(vm, "djb2.update: data must be a string");

    /* 3. userdata_data 返回 ud->data，安全转换为私有结构体指针 */
    HashCtx* ctx = (HashCtx*)g_api->userdata_data(argv[0]);
    const char* s = g_api->val_to_cstring(argv[1]);
    int len = g_api->string_len(argv[1]);

    /* 4. 操作私有状态 */
    for (int i = 0; i < len; i++)
        ctx->state = ctx->state * 33 ^ (unsigned char)s[i];

    return g_api->make_nil();
}
```

`fn_digest` 和 `fn_reset` 只涉及 `userdata_data` 和标量读写，结构完全对称。

---

## 5. v1 访问模式：模块函数而非方法

`MsModuleApi v1` 不支持在 Userdata 上注册方法（即 `h.update(data)` 语法）；
访问方式是把句柄作为第一个参数传给模块函数：

```ms
// v1：函数式调用
djb2.update(h, "hello")
djb2.digest(h)

// v2（计划）：方法调用（需要 MsModuleApi v2 的 userdata_bind_method）
// h.update("hello")
// h.digest()
```

这是 v1 的明确限制，与第 06 章（异步）、第 07 章（字节流）的 v1 约束一致。

---

## 6. 完整 `ms_module_init`

```c
MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "djb2: requires MsModuleApi v1");
        return;
    }
    g_api = api;
    api->def_native(vm, mod, "create", fn_create, 0);
    api->def_native(vm, mod, "update", fn_update, 2);
    api->def_native(vm, mod, "digest", fn_digest, 1);
    api->def_native(vm, mod, "reset",  fn_reset,  1);
}
```

---

## 7. 运行示例

```bash
cmake -S docs/features/capi/manual/examples \
      -B docs/features/capi/manual/examples/build
cmake --build docs/features/capi/manual/examples/build --config Release

MSLANG_PATH=docs/features/capi/manual/examples/build/02-userdata-hash/Release \
  ./build/mslang-c docs/features/capi/manual/examples/02-userdata-hash/run.ms
```

预期输出：

```
-4677384763180952763
-4677384763180952763
193409669
```

Windows（PowerShell）：

```powershell
$env:MSLANG_PATH = "docs\features\capi\manual\examples\build\02-userdata-hash\Release"
.\build\mslang-c.exe docs\features\capi\manual\examples\02-userdata-hash\run.ms
```

---

## 进一步阅读

- **值与错误处理**：[04-values-and-errors.md](04-values-and-errors.md)
- **MsModuleApi 完整定义**（userdata 分组）：[CAPI-05-module-api.md](../CAPI-05-module-api.md)
- **句柄类型规格**：[CAPI-06-handle-types.md](../CAPI-06-handle-types.md)
- **异步扩展**：[06-async-extensions.md](06-async-extensions.md)
