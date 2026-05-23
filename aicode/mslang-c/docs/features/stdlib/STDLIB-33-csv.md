# STDLIB-33: csv 模块

## 职责

CSV（逗号分隔值）的读写：支持自定义分隔符、引号转义、带标题的字典模式。
对应 Go 的 `encoding/csv` 包。`.ms` 实现。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/csv.ms`）。基于 `strings`/`io`/`bufio` 模块。

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `csv.read_str(s, sep=',', quote='"')` | str,str,str | list[list] | 解析 CSV 字符串 → 行列表 |
| `csv.read_file(path, sep=',')` | str, str | list[list] | 读文件 → 行列表 |
| `csv.read_dict(path, sep=',')` | str, str | list[map] | 读文件（首行为 header）→ 字典列表 |
| `csv.write_str(rows, sep=',')` | list[list], str | str | 行列表 → CSV 字符串 |
| `csv.write_file(path, rows, sep=',')` | str, list[list], str | nil | 写入文件 |
| `csv.reader(file, sep=',')` | File, str | CsvReader | 迭代器式读取器 |
| `csv.writer(file, sep=',')` | File, str | CsvWriter | 写入器 |

### CsvReader 方法

| 方法 | 返回 | 描述 |
|---|---|---|
| `cr.read_row()` | list\|nil | 读一行，EOF 返回 nil |
| `cr.read_all()` | list[list] | 读所有行 |

### CsvWriter 方法

| 方法 | 参数 | 描述 |
|---|---|---|
| `cw.write_row(row)` | list | 写一行 |
| `cw.write_all(rows)` | list[list] | 写所有行 |

---

## 依赖

- `strings` 模块（STDLIB-13）
- `io`/`bufio` 模块（STDLIB-04/30）

---

## 示例

```ms
import "csv"

var data = csv.read_str("a,b,c\n1,2,3\n4,5,6")
print(data)  // [["a","b","c"],["1","2","3"],["4","5","6"]]

var dicts = csv.read_dict("data.csv")
// [{a:"1", b:"2", c:"3"}, ...]
print(dicts[0]["a"])

print(csv.write_str([["name","age"],["Alice","30"]]))
// name,age\nAlice,30\n
```

---

## 测试

```
tests/unit/test_stdlib_csv.c
tests/fixtures/stdlib_csv_basic.ms
```
