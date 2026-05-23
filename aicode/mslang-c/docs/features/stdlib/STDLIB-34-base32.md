# STDLIB-34: base32 模块

## 职责

Base32 编解码（RFC 4648）：标准字母表（A-Z2-7）和扩展十六进制字母表（0-9A-V）。
`.ms` 实现（算法简单，无性能关键路径）。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/base32.ms`）。依赖 `buffer` 模块。

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `base32.encode(data)` | str\|buffer | str | 标准 Base32，含 `=` 填充 |
| `base32.decode(s)` | str | buffer | 解码标准 Base32 |
| `base32.encode_hex(data)` | str\|buffer | str | 扩展 hex 字母表（0-9A-V）|
| `base32.decode_hex(s)` | str | buffer | 解码扩展 hex Base32 |
| `base32.encode_raw(data)` | str\|buffer | str | 标准字母表，无填充 |
| `base32.is_valid(s)` | str | bool | 检查合法性 |

---

## 依赖

- `buffer` 模块（STDLIB-05 ✅）
- `strings` 模块（STDLIB-13，字母表查找）

---

## 示例

```ms
import "base32"

var s = base32.encode("Hello")
print(s)               // JBSWY3DP

import "buffer"
print(buffer.to_str(base32.decode(s)))  // Hello
```

---

## 测试

```
tests/unit/test_stdlib_base32.c
tests/fixtures/stdlib_base32_basic.ms
```
