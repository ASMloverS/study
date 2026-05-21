# 10 — MsModuleApi 参考

本章是 `MsModuleApi v1` 的完整函数参考，按五个功能分组排列。
术语和签名以 [CAPI-05-module-api.md](../CAPI-05-module-api.md) 为权威；
本章与 CAPI-05 不一致时，以 CAPI-05 为准。

```c
/* 版本号 */
uint32_t api->version;   /* = MS_MODULE_API_VERSION (当前 1) */
```

---

## 1. 注册

#### `api->def_native`

```c
void def_native(MsVM* vm, MsObjModule* mod,
                const char* name, MsNativeFn fn, int arity);
```

将单个 native 函数 `fn` 以 `name` 注册到 `mod->exports`。`arity` 是期望参数数；`-1` 表示可变参，VM 不做数量检查。

```c
api->def_native(vm, mod, "greet", fn_greet, 1);
```

**边界**：`name` 重复注册时后者覆盖前者。`name` 的生命周期只需持续到本调用返回（内部复制为 `ObjString`）。

---

#### `api->register_natives`

```c
void register_natives(MsVM* vm, MsObjModule* mod,
                      const MsNativeDef* defs);
```

批量注册：遍历 `defs` 数组直到 `defs[i].name == NULL`（哨兵）。等价于对每项调用 `def_native`。

```c
static const MsNativeDef kDefs[] = {
    {"add", fn_add, 2},
    {"sub", fn_sub, 2},
    {NULL, NULL, 0}
};
api->register_natives(vm, mod, kDefs);
```

---

#### `api->export_value`

```c
void export_value(MsVM* vm, MsObjModule* mod,
                  const char* name, MsValue v);
```

将任意 `MsValue`（整数、字符串、Userdata 等）挂到模块作用域，`.ms` 端通过 `mod.name` 访问。与 `def_native` 的区别：可挂非函数值。

```c
api->export_value(vm, mod, "VERSION", api->make_int(1));
```

---

## 2. 值构造

#### `api->make_nil`

```c
MsValue make_nil(void);
```

返回 nil 值。不分配堆内存。

---

#### `api->make_bool`

```c
MsValue make_bool(bool b);
```

返回 `true` 或 `false`。不分配堆内存。

---

#### `api->make_int`

```c
MsValue make_int(int64_t i);
```

返回 64 位有符号整数值。不分配堆内存。

---

#### `api->make_number`

```c
MsValue make_number(double d);
```

返回双精度浮点数值。不分配堆内存。

---

#### `api->make_string`

```c
MsValue make_string(MsVM* vm, const char* s, int len);
```

从字节缓冲区 `s[0..len)` 构造 VM 管理的字符串对象（拷贝内容，`s` 可在调用后立即释放）。**触发分配**，调用后 `val_to_cstring` 旧指针失效。

```c
char buf[64];
int n = snprintf(buf, sizeof(buf), "x=%d", val);
MsValue s = api->make_string(vm, buf, n);
```

**边界**：`len = 0` 返回空字符串；`s = NULL` 且 `len = 0` 亦合法。不做 NUL 终止检查（支持二进制内容）。

---

#### `api->make_list`

```c
MsValue make_list(MsVM* vm);
```

创建空列表对象。**触发分配**。

---

#### `api->make_map`

```c
MsValue make_map(MsVM* vm);
```

创建空哈希表对象。**触发分配**。

---

#### `api->list_push`

```c
void list_push(MsVM* vm, MsValue list, MsValue v);
```

将 `v` 追加到 `list` 尾部。`list` 必须是 `is_list` 为真的值，否则行为未定义。**触发分配**（容量扩展时）。

```c
MsValue lst = api->make_list(vm);
api->list_push(vm, lst, api->make_int(42));
```

---

#### `api->map_set`

```c
void map_set(MsVM* vm, MsValue map, MsValue key, MsValue val);
```

在 `map` 中写入键值对。`key` 通常为字符串；`map` 必须是 `is_map` 为真的值。**触发分配**（哈希表扩容时）。

```c
MsValue m = api->make_map(vm);
api->map_set(vm, m, api->make_string(vm, "k", 1), api->make_int(1));
```

---

## 3. 错误与异步

#### `api->raise`

```c
MsValue raise(MsVM* vm, const char* fmt, ...);
```

以 `printf` 格式字符串向 VM 发出运行时错误信号，返回哨兵 `MsValue`（调用方不应使用此返回值）。调用后**必须立即 `return`**；禁止继续访问任何 `MsValue`/`MsObject`。

```c
if (argc < 1)
    return api->raise(vm, "expected 1 argument, got 0");
```

**边界**：格式化缓冲区上限约 512 字节；超出部分截断。

---

#### `api->make_future`

```c
MsValue make_future(MsVM* vm);
```

创建处于 `PENDING` 状态的 Future 对象。**触发分配**。调用方在启动异步操作前应 pin（固定）此 Future 防止 GC 回收（v1 下 pin 机制由调用方自行管理，通常保存到 Userdata 或全局变量）。

---

#### `api->future_resolve`

```c
void future_resolve(MsVM* vm, MsValue fut, MsValue val);
```

将 `fut` 置为 `FULFILLED` 并附带结果 `val`。**只能在 VM 主线程调用**（不得从 worker 线程直接调用）；v1 下无线程安全保证。

---

#### `api->future_reject`

```c
void future_reject(MsVM* vm, MsValue fut, MsValue err);
```

将 `fut` 置为 `REJECTED` 并附带错误 `err`（通常为字符串）。限制与 `future_resolve` 相同。

---

## 4. Userdata

#### `api->userdata_new`

```c
MsValue userdata_new(MsVM* vm, size_t bytes,
                     void (*finalize)(void* data),
                     void (*mark)(MsVM* vm, void* data),
                     const char* type_tag);
```

分配 `bytes` 字节的私有数据块并创建 `MS_OBJ_USERDATA` 对象。**触发分配**。

| 参数 | 说明 |
|---|---|
| `bytes` | 私有数据大小；`0` 合法（`data` 为 `NULL`） |
| `finalize` | GC 回收时调用；禁止在其内调用任何 `api->*`；传 `NULL` 表示无需清理 |
| `mark` | GC mark 阶段调用；仅当 data 内存储 `MsValue` 引用时需要；传 `NULL` 表示无引用 |
| `type_tag` | 静态生存期字符串字面量，运行时类型标识 |

```c
static const char* kTag = "myext.Handle";
MsValue ud = api->userdata_new(vm, sizeof(MyCtx), NULL, NULL, kTag);
MyCtx* ctx = (MyCtx*)api->userdata_data(ud);
```

**边界**：GC 在 `finalize` 返回后自动释放 `data` 缓冲区，无需在 `finalize` 内 `free(data)`。

---

#### `api->userdata_data`

```c
void* userdata_data(MsValue v);
```

返回 `v` 内部 `data` 指针（即 `userdata_new` 分配的私有缓冲区）。`v` 必须是 Userdata，否则返回 `NULL`。

---

#### `api->userdata_tag`

```c
const char* userdata_tag(MsValue v);
```

返回 `v` 的 `type_tag` 字符串。`v` 必须是 Userdata。返回指针与 `userdata_new` 传入的 `type_tag` 相同（不拷贝）。

---

#### `api->userdata_is`

```c
bool userdata_is(MsValue v, const char* tag);
```

检查 `v` 是否为 Userdata 且 `type_tag` 与 `tag` 匹配。内部先走指针相等（fast path），失败时 fallback 到 `strcmp`（跨 DLL 边界时可靠）。

```c
if (!api->userdata_is(argv[0], kTag))
    return api->raise(vm, "expected a myext.Handle");
```

---

## 5. 值解包与容器

### 5.1 类型谓词

以下函数均返回 `bool`，无副作用，不触发分配：

| 函数 | 签名 | 说明 |
|---|---|---|
| `is_nil` | `bool is_nil(MsValue v)` | v 是 nil |
| `is_bool` | `bool is_bool(MsValue v)` | v 是布尔值 |
| `is_int` | `bool is_int(MsValue v)` | v 是 int64 |
| `is_number` | `bool is_number(MsValue v)` | v 是 double |
| `is_string` | `bool is_string(MsValue v)` | v 是字符串对象 |
| `is_list` | `bool is_list(MsValue v)` | v 是列表对象 |
| `is_map` | `bool is_map(MsValue v)` | v 是哈希表对象 |
| `is_tuple` | `bool is_tuple(MsValue v)` | v 是元组对象 |
| `is_function` | `bool is_function(MsValue v)` | v 是函数/闭包/native |
| `is_userdata` | `bool is_userdata(MsValue v, const char* tag)` | v 是指定 tag 的 Userdata |

**规则**：必须先 `is_*` 检查，再调用对应 `val_to_*` 解包；跳过检查直接解包为未定义行为。

### 5.2 值解包

```c
bool        val_to_bool(MsValue v);    /* v 必须 is_bool */
int64_t     val_to_int(MsValue v);     /* v 必须 is_int  */
double      val_to_number(MsValue v);  /* v 必须 is_number */
const char* val_to_cstring(MsValue v); /* v 必须 is_string；见生命周期说明 */
int         string_len(MsValue v);     /* v 必须 is_string；返回字节数 */
```

**`val_to_cstring` 生命周期**：返回指针有效直到「下一次调用任何触发分配的 `api->*` 函数」或「native 函数返回」（以先发生者为准）。触发分配的 api 包括 `make_string`、`make_list`、`make_map`、`list_push`、`map_set`、`userdata_new`。跨此边界访问为未定义行为；需长期持有时须拷贝到私有缓冲区。

### 5.3 容器访问

#### `api->list_len`

```c
int list_len(MsValue v);   /* v 必须 is_list */
```

返回列表元素数。

#### `api->list_get`

```c
MsValue list_get(MsValue v, int idx);   /* v 必须 is_list */
```

返回索引 `idx` 处的元素。`idx` 越界时返回 `nil`（不 raise）。

#### `api->map_get`

```c
bool map_get(MsValue v, MsValue key, MsValue* out);   /* v 必须 is_map */
```

在 `v` 中查找 `key`。命中时将值写入 `*out` 并返回 `true`；未命中返回 `false`，`*out` 不修改。`key` 通常为字符串值。

```c
MsValue val;
if (api->map_get(map, api->make_string(vm, "x", 1), &val)) {
    /* 使用 val */
}
```

---

## 进一步阅读

- **完整 struct 定义**：[CAPI-05-module-api.md](../CAPI-05-module-api.md)
- **Hello 扩展教程**（注册流程）：[03-hello-extension.md](03-hello-extension.md)
- **值与错误处理**（raise 协议、val_to_cstring 生命周期）：[04-values-and-errors.md](04-values-and-errors.md)
- **Userdata 句柄**（finalize/mark 详解）：[05-userdata-handles.md](05-userdata-handles.md)
- **异步扩展**（Future 使用模式）：[06-async-extensions.md](06-async-extensions.md)
