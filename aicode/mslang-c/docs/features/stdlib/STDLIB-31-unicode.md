# STDLIB-31: unicode 模块

## 职责

Unicode 代码点属性查询、UTF-8/UTF-16 编解码工具。
对应 Go 的 `unicode`、`unicode/utf8`、`unicode/utf16` 包。

---

## C/.ms 分层

全部 C（`src/stdlib/unicode.c`）。
包含紧凑 Unicode 属性表（类别/大小写转换/分类）。

---

## 函数清单（摘要）

### 属性判断（接受 code point int 或单字符 str）

| 函数 | 返回 | 描述 |
|---|---|---|
| `unicode.is_letter(c)` | bool | Unicode 字母（含 CJK）|
| `unicode.is_digit(c)` | bool | Unicode 十进制数字 |
| `unicode.is_space(c)` | bool | Unicode 空白 |
| `unicode.is_upper(c)` | bool | 大写字母 |
| `unicode.is_lower(c)` | bool | 小写字母 |
| `unicode.is_punct(c)` | bool | 标点 |
| `unicode.is_graphic(c)` | bool | 可打印非空白 |

### 大小写转换

| 函数 | 描述 |
|---|---|
| `unicode.to_upper(c)` → str | 代码点转大写（Unicode 折叠）|
| `unicode.to_lower(c)` → str | 代码点转小写 |
| `unicode.to_title(c)` → str | 标题大小写 |

### UTF-8

| 函数 | 描述 |
|---|---|
| `unicode.rune_count(s)` → int | 字符串中 Unicode 代码点数量 |
| `unicode.rune_at(s, i)` → str | 第 i 个代码点（单字符字符串）|
| `unicode.runes(s)` → list | 所有代码点列表（单字符字符串）|
| `unicode.encode_utf8(cp)` → buffer | 代码点 → UTF-8 字节 |
| `unicode.decode_utf8(buf, offset=0)` → [str, int] | 解码一个代码点 + 字节数 |
| `unicode.valid_utf8(s)` → bool | 是否为合法 UTF-8 |

### 代码点

| 函数 | 描述 |
|---|---|
| `unicode.ord(c)` → int | 字符 → 代码点整数（`len(c)==1`）|
| `unicode.chr(n)` → str | 代码点整数 → 字符 |

---

## 依赖

- CAPI-01/02
- Unicode 属性数据表（编译期嵌入，紧凑编码约 32KB）

---

## 测试

```
tests/unit/test_stdlib_unicode.c
tests/fixtures/stdlib_unicode_basic.ms
```
