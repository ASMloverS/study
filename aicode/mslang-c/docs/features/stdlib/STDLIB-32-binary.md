# STDLIB-32: binary 模块

## 职责

二进制数值的打包/解包（大小端）、varint 编解码（LEB128）。
对应 Go 的 `encoding/binary` 包。

---

## C/.ms 分层

全部 C（`src/stdlib/binary.c`）。操作 `ObjBuffer` 原始字节。

---

## 格式字符串

类似 Python `struct` 格式，前缀决定字节序：

| 前缀 | 字节序 |
|---|---|
| `>` | 大端（big-endian）|
| `<` | 小端（little-endian）|
| `=` | 本机字节序 |

| 格式字符 | 类型 | 大小 |
|---|---|---|
| `b`/`B` | int8/uint8 | 1 |
| `h`/`H` | int16/uint16 | 2 |
| `i`/`I` | int32/uint32 | 4 |
| `q`/`Q` | int64/uint64 | 8 |
| `f` | float32 | 4 |
| `d` | float64 | 8 |
| `s` | 原始字节（需指定长度前缀，如 `4s`）| N |

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `binary.pack(fmt, ...)` | str, any... | buffer | 按 fmt 打包值为字节 |
| `binary.unpack(fmt, buf, offset=0)` | str, buffer, int | list | 从 buf+offset 解包 |
| `binary.pack_into(buf, offset, fmt, ...)` | buffer,int,str,any... | nil | 原地写入 buf |
| `binary.calc_size(fmt)` | str | int | 格式对应的字节数 |
| `binary.varint_encode(n)` | int | buffer | LEB128 无符号 varint |
| `binary.varint_decode(buf, offset=0)` | buffer, int | [int, int] | 解码 + 消耗字节数 |
| `binary.svarint_encode(n)` | int | buffer | LEB128 有符号 zigzag |
| `binary.svarint_decode(buf, offset=0)` | buffer, int | [int, int] | 解码有符号 varint |

单值便捷函数：
`binary.read_u8/u16_le/u16_be/u32_le/u32_be/u64_le/u64_be(buf, offset)` → int

---

## 依赖

- CAPI-01/02
- `ObjBuffer`（`include/ms/stdlib/objbuffer.h`）

---

## 示例

```ms
import "binary"
import "buffer"

var b = binary.pack(">IH", 0xDEADBEEF, 0x1234)
print(buffer.to_hex(b))  // deadbeef1234

var vals = binary.unpack(">IH", b)
print(vals[0])  // 3735928559 (0xDEADBEEF)
print(vals[1])  // 4660 (0x1234)

var vb = binary.varint_encode(300)
print(buffer.to_hex(vb))  // ac02
```

---

## 测试

```
tests/unit/test_stdlib_binary.c
tests/fixtures/stdlib_binary_basic.ms
```
