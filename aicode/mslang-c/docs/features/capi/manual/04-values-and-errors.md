# 04 — 值与错误处理

本章讲解如何在 native 函数中构造 `MsValue`、检查参数类型，以及正确使用错误处理 API。
对应示例工程 `examples/01-values-errors/`，扩展名为 `greet`。

---

## 目标

写一个名为 `greet` 的扩展，暴露四个演示不同 API 用法的函数：

```ms
import greet

print(greet.greet("World"))        # Hello, World!
print(greet.type_of(42))           # int
print(greet.add(1, 2.5))           # 3.5

s = greet.summary("Alice", 85)
print(s["name"])                   # Alice
print(s["passed"])                 # true
```

---

## 1. 值构造

`MsModuleApi` 为每种基础类型提供对应的构造函数：

| 构造函数 | 签名 | 说明 |
|---|---|---|
| `make_nil` | `() -> MsValue` | nil 值（无需 vm）|
| `make_bool` | `(bool) -> MsValue` | true / false |
| `make_int` | `(int64_t) -> MsValue` | 64 位整数 |
| `make_number` | `(double) -> MsValue` | 浮点数 |
| `make_string` | `(vm, s, len) -> MsValue` | 拷贝字节串，分配 GC 对象 |
| `make_list` | `(vm) -> MsValue` | 空列表，分配 GC 对象 |
| `make_map` | `(vm) -> MsValue` | 空映射，分配 GC 对象 |
| `list_push` | `(vm, list, v)` | 追加元素到列表（触发分配）|
| `map_set` | `(vm, map, key, val)` | 设置键值（触发分配）|

前四个（nil/bool/int/number）不触发 GC 分配；后五个会触发分配，影响 `val_to_cstring` 指针的有效性（见第 4 节）。

---

## 2. 值检查与解包

先用 `is_*` 检查类型，再用 `val_to_*` 解包——**禁止在未检查时直接解包**：

```c
/* 错误示范：未检查直接解包 */
const char* s = g_api->val_to_cstring(argv[0]);  /* 若非 string，行为未定义 */

/* 正确示范 */
if (!g_api->is_string(argv[0]))
    return g_api->raise(vm, "expected string");
const char* s = g_api->val_to_cstring(argv[0]);
```

`fn_type_of` 演示全部 `is_*` 检查：

```c
static MsValue fn_type_of(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1)
        return g_api->raise(vm, "type_of(val): expected 1 argument");
    MsValue v = argv[0];
    const char* t;
    if      (g_api->is_nil(v))    t = "nil";
    else if (g_api->is_bool(v))   t = "bool";
    else if (g_api->is_int(v))    t = "int";
    else if (g_api->is_number(v)) t = "number";
    else if (g_api->is_string(v)) t = "string";
    else if (g_api->is_list(v))   t = "list";
    else if (g_api->is_map(v))    t = "map";
    else                          t = "other";
    return g_api->make_string(vm, t, (int)strlen(t));
}
```

---

## 3. 参数校验范式与 raise 协议

标准校验模式：先检查 `argc`，再逐个检查类型：

```c
static MsValue fn_add(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2)
        return g_api->raise(vm, "add(a, b): expected 2 arguments");
    int a_ok = g_api->is_int(argv[0]) || g_api->is_number(argv[0]);
    int b_ok = g_api->is_int(argv[1]) || g_api->is_number(argv[1]);
    if (!a_ok || !b_ok)
        return g_api->raise(vm, "add(a, b): expected numbers");
    /* ... */
}
```

**raise 协议**：
- `api->raise` 后**必须立即 `return`**。raise 返回一个哨兵 `MsValue`，继续执行会导致 VM 状态不一致。
- `return g_api->raise(...)` 是惯用写法，一行完成抛错和返回。
- raise 后禁止访问任何 `MsValue` 或 `MsObject`。

---

## 4. `val_to_cstring` 内存所有权

`val_to_cstring` 返回的指针直接指向 GC 管理的字符串对象内部。以下操作会触发 GC 分配，使该指针失效：

`make_string` · `make_list` · `make_map` · `list_push` · `map_set` · `userdata_new`

**规则**：触发分配前必须完成对指针的所有使用，或先将内容拷贝到私有缓冲区：

```c
/* 错误示范：make_map 后继续使用 name_ptr */
const char* name_ptr = g_api->val_to_cstring(argv[0]);
MsValue map = g_api->make_map(vm);          /* 触发分配，name_ptr 可能已失效 */
g_api->make_string(vm, name_ptr, ...);      /* 未定义行为 */

/* 正确示范：分配前拷贝到栈缓冲区 */
const char* name_ptr = g_api->val_to_cstring(argv[0]);
char name_buf[256];
snprintf(name_buf, sizeof(name_buf), "%s", name_ptr);
MsValue map = g_api->make_map(vm);          /* 分配后使用 name_buf，安全 */
g_api->make_string(vm, name_buf, (int)strlen(name_buf));
```

同理，`val_to_cstring` 指针在 native 函数**返回后**也不再有效；需要长期持有的字符串必须拷贝到扩展自己的堆内存。

---

## 5. 构造复合值

`fn_summary` 组合 `make_map`、`make_list`、`map_set`、`list_push`，同时演示安全的 val_to_cstring 用法：

```c
static MsValue fn_summary(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2)
        return g_api->raise(vm, "summary(name, score): expected 2 arguments");
    if (!g_api->is_string(argv[0]))
        return g_api->raise(vm, "summary: name must be a string");
    if (!g_api->is_int(argv[1]) && !g_api->is_number(argv[1]))
        return g_api->raise(vm, "summary: score must be a number");

    /* 先拷贝 name，make_map 会触发分配使 val_to_cstring 指针失效 */
    const char* name_ptr = g_api->val_to_cstring(argv[0]);
    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "%s", name_ptr);

    double score = g_api->is_int(argv[1])
        ? (double)g_api->val_to_int(argv[1])
        : g_api->val_to_number(argv[1]);

    MsValue map = g_api->make_map(vm);

    g_api->map_set(vm, map,
        g_api->make_string(vm, "name", 4),
        g_api->make_string(vm, name_buf, (int)strlen(name_buf)));

    g_api->map_set(vm, map,
        g_api->make_string(vm, "score", 5),
        argv[1]);

    g_api->map_set(vm, map,
        g_api->make_string(vm, "passed", 6),
        g_api->make_bool(score >= 60.0));

    MsValue tags = g_api->make_list(vm);
    g_api->list_push(vm, tags, g_api->make_string(vm, "student", 7));
    g_api->map_set(vm, map, g_api->make_string(vm, "tags", 4), tags);

    return map;
}
```

---

## 6. 运行示例

```bash
cmake -S docs/features/capi/manual/examples \
      -B docs/features/capi/manual/examples/build
cmake --build docs/features/capi/manual/examples/build --config Release

MSLANG_PATH=docs/features/capi/manual/examples/build/01-values-errors \
  ./build/mslang-c docs/features/capi/manual/examples/01-values-errors/run.ms
```

预期输出：

```
Hello, World!
int
number
string
nil
bool
3
3.5
Alice
85
true
student
```

Windows（PowerShell，MSVC 多配置构建输出在 `Release\` 子目录）：

```powershell
$env:MSLANG_PATH = "docs\features\capi\manual\examples\build\01-values-errors\Release"
.\build\mslang-c.exe docs\features\capi\manual\examples\01-values-errors\run.ms
```

---

## 进一步阅读

- **Hello 扩展全流程**：[03-hello-extension.md](03-hello-extension.md)
- **MsModuleApi 完整定义**：[CAPI-05-module-api.md](../CAPI-05-module-api.md)
- **Userdata 句柄**：[05-userdata-handles.md](05-userdata-handles.md)
