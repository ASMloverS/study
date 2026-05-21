# 07 — 字节流互操（v1）

本章说明 `MsModuleApi v1` 下第三方扩展与字节流数据互操的可行路径，
并通过 `examples/04-bytes-interop/` 演示「接收 ObjString、操作字节、返回新 ObjString」模式。

---

## 1. v1 限制声明

`MsModuleApi v1` 函数表不包含 `is_file` / `is_buffer`，
因此**第三方扩展无法直接识别或操作 `ObjFile` / `ObjBuffer` 对象**。

需要直接构造 `ObjFile*` / `ObjBuffer*` 的场景属于内置模块作者范畴，
见第 08 章。

---

## 2. v1 可行路径：ObjString 字节流契约

当 `.ms` 侧调用者持有原始字节数据时，最直接的做法是将数据包装成字符串再传给扩展：

```ms
data = "abc"                    # 或来自 io.read_file 的二进制内容
result = bufops.hex_encode(data)
```

扩展侧通过三个 API 读取字节流：

| API | 作用 |
|---|---|
| `api->is_string(v)` | 检查 v 是否为字符串（必须先检查再解包） |
| `api->string_len(v)` | 返回**字节数**（非 Unicode 码点数） |
| `api->val_to_cstring(v)` | 返回指向底层字节的 C 指针 |

`val_to_cstring` 返回的指针有效期：直到「下一次调用任何触发分配的 `api->*` 函数」。
触发分配的 api 包括 `make_string`、`make_list`、`make_map`、`list_push`、`map_set`、`userdata_new`。
跨此边界访问为未定义行为；需要持有更长时间时须拷贝到私有缓冲区。

---

## 3. 示例：`examples/04-bytes-interop/`

扩展名为 `bufops`，演示三种字节操作：

```ms
import "bufops"

print(bufops.to_upper("hello"))      # HELLO
print(bufops.hex_encode("abc"))      # 616263
print(bufops.byte_count("hello"))    # 5
```

### 3.1 读取字节并转换：`to_upper`

```c
static MsValue fn_to_upper(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !g_api->is_string(argv[0]))
        return g_api->raise(vm, "to_upper(s): expected string");

    /* string_len 返回字节数；val_to_cstring 指针在 make_string 调用前有效 */
    int         len = g_api->string_len(argv[0]);
    const char* src = g_api->val_to_cstring(argv[0]);

    char* buf = (char*)malloc((size_t)len);
    if (!buf)
        return g_api->raise(vm, "to_upper: out of memory");

    for (int i = 0; i < len; i++)
        buf[i] = (char)toupper((unsigned char)src[i]);

    /* make_string 是此函数内第一个触发分配的 api 调用，在此之前 src 安全 */
    MsValue result = g_api->make_string(vm, buf, len);
    free(buf);
    return result;
}
```

**关键点**：

- `string_len` + `val_to_cstring` 都不触发分配，调用顺序任意。
- `malloc` 是普通 C 调用，不触发 VM 分配，`src` 仍有效。
- `make_string` 是第一个触发 VM 分配的调用，在此之后 `src` 失效；
  但此时 `src` 已读完，无影响。

### 3.2 输出大于输入：`hex_encode`

```c
static MsValue fn_hex_encode(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !g_api->is_string(argv[0]))
        return g_api->raise(vm, "hex_encode(s): expected string");

    int         len = g_api->string_len(argv[0]);
    const char* src = g_api->val_to_cstring(argv[0]);

    /* 输出长度是输入字节数的 2 倍 */
    int   out_len = len * 2;
    char* buf     = (char*)malloc((size_t)out_len + 1);
    if (!buf)
        return g_api->raise(vm, "hex_encode: out of memory");

    for (int i = 0; i < len; i++)
        snprintf(buf + i * 2, 3, "%02x", (unsigned char)src[i]);

    MsValue result = g_api->make_string(vm, buf, out_len);
    free(buf);
    return result;
}
```

此模式适用于所有「输出字节数与输入不同」的变换（压缩、编码、加密等），
只需按实际输出长度分配私有缓冲区，然后一次性 `make_string`。

### 3.3 仅查询：`byte_count`

```c
static MsValue fn_byte_count(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !g_api->is_string(argv[0]))
        return g_api->raise(vm, "byte_count(s): expected string");
    return g_api->make_int(g_api->string_len(argv[0]));
}
```

`make_int` 不触发分配（返回栈上值），`val_to_cstring` 此处完全不需要。

---

## 4. 运行示例

```bash
cmake -S docs/features/capi/manual/examples \
      -B docs/features/capi/manual/examples/build
cmake --build docs/features/capi/manual/examples/build --config Release

MSLANG_PATH=docs/features/capi/manual/examples/build/04-bytes-interop/Release \
  ./build/mslang-c docs/features/capi/manual/examples/04-bytes-interop/run.ms
```

预期输出：

```
HELLO
MSLANG-C!
616263
486921
5
0
```

Windows（PowerShell）：

```powershell
$env:MSLANG_PATH = "docs\features\capi\manual\examples\build\04-bytes-interop\Release"
.\build\mslang-c.exe docs\features\capi\manual\examples\04-bytes-interop\run.ms
```

---

## 进一步阅读

- **值与错误处理**（`val_to_cstring` 生命周期详述）：[04-values-and-errors.md](04-values-and-errors.md)
- **内置模块作者指南**（`ObjFile*` / `ObjBuffer*` 直接构造）：[08-builtin-module-author.md](08-builtin-module-author.md)
- **File/Buffer 设计规格**：[CAPI-06-file-buffer-userdata.md](../CAPI-06-file-buffer-userdata.md)
