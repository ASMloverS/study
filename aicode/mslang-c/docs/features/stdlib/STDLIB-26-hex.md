# STDLIB-26: hex 模块

## 职责

十六进制编解码：字节/字符串 ↔ hex 字符串。轻量实现，复用 `buffer` 模块已有的 `to_hex` 方法。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/hex.ms`）。基于 `buffer` 模块；`buffer.to_hex()` 已在 C 层实现。

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `hex.encode(data)` | str\|buffer | str | 编码为小写 hex 字符串 |
| `hex.encode_upper(data)` | str\|buffer | str | 编码为大写 hex 字符串 |
| `hex.decode(s)` | str | buffer | hex 字符串解码为原始字节 |
| `hex.is_valid(s)` | str | bool | 是否为合法 hex 字符串（偶数位，0-9a-fA-F）|
| `hex.byte_to_hex(n)` | int | str | 单字节（0–255）→ 2 位 hex |
| `hex.hex_to_byte(s)` | str | int | 2 位 hex → 单字节（0–255）|

---

## 依赖

- `buffer` 模块（STDLIB-05 ✅，`to_hex`/`from_hex` 方法）

---

## 示例

```ms
import "hex"

print(hex.encode("Hello"))         // 48656c6c6f
print(hex.encode_upper("Hello"))   // 48656C6C6F

var buf = hex.decode("48656c6c6f")
import "buffer"
print(buffer.to_str(buf))          // Hello

print(hex.is_valid("48656c6c6f"))  // true
print(hex.is_valid("48656g6c6f"))  // false (非法字符 g)

print(hex.byte_to_hex(255))        // ff
print(hex.hex_to_byte("ff"))       // 255
```

---

## 测试

```
tests/unit/test_stdlib_hex.c
tests/fixtures/stdlib_hex_basic.ms
```
