# STDLIB-15: fmt 模块

## 职责

格式化字符串输出：`sprintf`（返回字符串）、`printf`（输出到 stdout）、`eprintf`（输出到 stderr）。
格式说明符为 C `printf` 的常用子集，不依赖 locale。

---

## C/.ms 分层

| 层 | 内容 | 文件 |
|---|---|---|
| C 原语（`_fmt`）| sprintf 内核：格式解析 + 类型分发 + 写入 | `src/stdlib/_fmt.c` |
| .ms 组合层（`fmt`）| `printf`（调 `_fmt.sprintf` + print）、`eprintf`、`format`（带类型名） | `stdlib/ms/fmt.ms` |

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `fmt.sprintf(format, ...)` | str, any... | str | 格式化字符串 |
| `fmt.printf(format, ...)` | str, any... | nil | 格式化后打印到 stdout（不加换行）|
| `fmt.println(format, ...)` | str, any... | nil | 格式化后打印，末尾加 `\n` |
| `fmt.eprintf(format, ...)` | str, any... | nil | 格式化后打印到 stderr |
| `fmt.format(value)` | any | str | 类似 `str(value)` 但包含类型信息（`<int: 42>`）|

---

## 支持的格式说明符

| 说明符 | 类型 | 描述 |
|---|---|---|
| `%d` / `%i` | int | 十进制整数 |
| `%u` | int | 无符号十进制 |
| `%x` / `%X` | int | 十六进制小写/大写 |
| `%o` | int | 八进制 |
| `%b` | int | 二进制 |
| `%f` | num | 十进制浮点（默认 6 位小数）|
| `%e` / `%E` | num | 科学计数法 |
| `%g` / `%G` | num | 最短精确表示 |
| `%s` | str | 字符串 |
| `%q` | str | 带引号的字符串（调 strconv.quote）|
| `%v` | any | 默认格式（等同 `str(v)`）|
| `%c` | int | Unicode 代码点 → 字符 |
| `%%` | — | 字面量 `%` |

修饰符：`-`（左对齐）、`+`（强制符号）、`0`（零填充）、宽度（`%10d`）、精度（`%.2f`）。

---

## 依赖

- `_fmt`（C 原语）
- `strconv`（`%q` 格式调用）

---

## 示例

```ms
import "fmt"

print(fmt.sprintf("Hello, %s! You are %d years old.", "Alice", 30))
// Hello, Alice! You are 30 years old.

fmt.printf("pi ≈ %.4f\n", 3.14159)    // pi ≈ 3.1416
fmt.println("hex: %x  bin: %b", 255, 10)  // hex: ff  bin: 1010
print(fmt.sprintf("%q", "line\n"))     // "line\n"
print(fmt.format(42))                  // <int: 42>
```

---

## 测试

```
tests/unit/test_stdlib_fmt.c
tests/fixtures/stdlib_fmt_basic.ms
```
