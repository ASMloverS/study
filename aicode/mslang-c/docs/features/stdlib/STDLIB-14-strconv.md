# STDLIB-14: strconv 模块

## 职责

字符串与基本数据类型之间的转换：整数/浮点数的解析与格式化、引号转义/反转义、布尔解析。
保证 locale 无关（不受 `LC_NUMERIC` 影响），结果可预期。

---

## C/.ms 分层

| 层 | 内容 | 文件 |
|---|---|---|
| C 原语（`_strconv`）| `strtoll`/`strtod`/`snprintf` 包装、`quote`/`unquote` | `src/stdlib/_strconv.c` |
| .ms 组合层（`strconv`）| `must_parse_*`（友好错误）、`try_parse_*`（返回 nil）、`parse_bool` 语义 | `stdlib/ms/strconv.ms` |

---

## 函数清单

### 整数

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strconv.format_int(n, base=10)` | int, int | str | 整数 → 字符串（base 2–36）|
| `strconv.parse_int(s, base=10)` | str, int | int | 解析整数，失败抛运行时错误 |
| `strconv.try_parse_int(s, base=10)` | str, int | int\|nil | 解析失败返回 nil |

### 浮点数

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strconv.format_float(x, fmt='g', prec=-1)` | num,str,int | str | 格式化（`'e'`/`'f'`/`'g'`）|
| `strconv.parse_float(s)` | str | num | 解析浮点，失败抛错误 |
| `strconv.try_parse_float(s)` | str | num\|nil | 解析失败返回 nil |

> `fmt='g'`：最短精确表示；`prec=-1`：使用默认精度（最短往返精度）。

### 布尔

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strconv.parse_bool(s)` | str | bool | `"true"/"1"/"yes"` → true；`"false"/"0"/"no"` → false；否则抛错 |
| `strconv.format_bool(b)` | bool | str | `"true"` / `"false"` |

### 引号与转义

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strconv.quote(s)` | str | str | 加双引号并转义（`\n`/`\t`/`\"`/`\\` 等）|
| `strconv.unquote(s)` | str | str | 去掉外层引号并反转义 |
| `strconv.quote_rune(c)` | str | str | 单字符加单引号，转义 |

---

## 依赖

- `_strconv`（C 原语）

---

## 示例

```ms
import "strconv"

print(strconv.format_int(255, 16))      // ff
print(strconv.parse_int("ff", 16))      // 255
print(strconv.format_float(3.14, 'f', 2)) // 3.14
print(strconv.try_parse_int("bad"))     // nil
print(strconv.quote("he said \"hi\""))  // "he said \"hi\""
print(strconv.parse_bool("yes"))        // true
```

---

## 测试

```
tests/unit/test_stdlib_strconv.c
tests/fixtures/stdlib_strconv_basic.ms
```
