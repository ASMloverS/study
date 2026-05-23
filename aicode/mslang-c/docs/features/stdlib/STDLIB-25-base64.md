# STDLIB-25: base64 模块

## 职责

Base64 编解码（RFC 4648）：标准字母表（`+/`）和 URL 安全字母表（`-_`），支持有/无填充。
纯 C 实现，无外部依赖。

---

## C/.ms 分层

全部 C（`src/stdlib/base64.c`）。内部 C 原语直接作为公开模块，无 .ms 包装层。

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `base64.encode(data)` | str\|buffer | str | 标准 Base64 编码，含 `=` 填充 |
| `base64.decode(s)` | str | buffer | 解码标准 Base64，失败抛错 |
| `base64.encode_url(data)` | str\|buffer | str | URL-safe Base64（`-_`，无填充）|
| `base64.decode_url(s)` | str | buffer | 解码 URL-safe Base64 |
| `base64.encode_raw(data)` | str\|buffer | str | 标准字母表，无填充 |
| `base64.decode_raw(s)` | str | buffer | 解码无填充 Base64 |
| `base64.is_valid(s)` | str | bool | 检查是否为合法 Base64（不解码）|
| `base64.encoded_len(n)` | int | int | n 字节编码后的字符数 |
| `base64.decoded_len(s)` | str | int | Base64 字符串解码后的最大字节数 |

---

## 输入处理

- `str` 输入：取 UTF-8 字节序列编码。
- `buffer` 输入：直接取原始字节。
- 解码输出：始终返回 `buffer`（原始字节），调用者按需转 str。

---

## 依赖

- CAPI-01/02（注册表）
- `ObjBuffer`（`include/ms/stdlib/objbuffer.h`，解码输出）
- `<stdint.h>` `<string.h>`

---

## 示例

```ms
import "base64"
import "buffer"

// 编码
var s = base64.encode("Hello, World!")
print(s)   // SGVsbG8sIFdvcmxkIQ==

// 解码
var buf = base64.decode("SGVsbG8sIFdvcmxkIQ==")
print(buffer.to_str(buf))   // Hello, World!

// URL-safe
var url_s = base64.encode_url("data with +/chars")
print(url_s)   // 无 + / 字符，无 = 填充

// 检验
print(base64.is_valid("SGVs"))       // true
print(base64.is_valid("SGVs$"))      // false
```

---

## 测试

```
tests/unit/test_stdlib_base64.c
tests/fixtures/stdlib_base64_basic.ms
```

关键测试点：
- 往返（encode→decode→to_str 等于原始字符串）
- 空字符串
- 长度非3倍数（填充正确性）
- URL-safe 不含 `+/=`
- 非法字符解码抛错
