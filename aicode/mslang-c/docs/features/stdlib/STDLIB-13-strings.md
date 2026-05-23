# STDLIB-13: strings 模块

## 职责

字符串工具函数集合，提供函数式风格的字符串操作（可作为高阶函数参数传递），
补充内置字符串方法（`s.split`/`s.replace`/`s.trim`…）未覆盖的功能：
`join`、`repeat`、`fields`、字符类判断、`to_bytes`/`from_bytes`、`pad` 等。

---

## C/.ms 分层

| 层 | 内容 | 文件 |
|---|---|---|
| C 原语（`_strprim`）| `join`/`repeat`/`fields`/`split_n`/`replace_n`（带 n 次限制）/`trim_chars`/`trim_left_chars`/`trim_right_chars`/`last_index`/`count` | `src/stdlib/_strprim.c` |
| .ms 组合层（`strings`）| 包装内置方法为函数 + 字符类判断 + `pad` + `to_bytes`/`from_bytes` | `stdlib/ms/strings.ms` |

---

## 函数清单

### 拼接与分割

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strings.join(list, sep)` | list[str], str | str | 用 sep 连接字符串列表 |
| `strings.split(s, sep)` | str, str | list | 与 `s.split(sep)` 等价的函数版 |
| `strings.split_n(s, sep, n)` | str, str, int | list | 最多切 n 份（最后一份含剩余）|
| `strings.fields(s)` | str | list | 按连续空白切分（忽略首尾空白）|
| `strings.repeat(s, n)` | str, int | str | 重复 n 次 |

### 查找与替换

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strings.contains(s, sub)` | str, str | bool | 是否含子串 |
| `strings.has_prefix(s, prefix)` | str, str | bool | 是否以 prefix 开头 |
| `strings.has_suffix(s, suffix)` | str, str | bool | 是否以 suffix 结尾 |
| `strings.index(s, sub)` | str, str | int | 第一次出现位置，未找到返回 -1 |
| `strings.last_index(s, sub)` | str, str | int | 最后一次出现位置 |
| `strings.count(s, sub)` | str, str | int | 不重叠出现次数 |
| `strings.replace(s, old, new, n=-1)` | str,str,str,int | str | 替换前 n 次（-1=全部）|
| `strings.replace_all(s, old, new)` | str,str,str | str | 全部替换（`replace_n=-1` 的别名）|

### 修剪与对齐

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strings.trim(s, chars)` | str, str | str | 两端删除 chars 中的字符 |
| `strings.trim_left(s, chars)` | str, str | str | 左侧删除 |
| `strings.trim_right(s, chars)` | str, str | str | 右侧删除 |
| `strings.trim_space(s)` | str | str | 删除两端空白（\\t\\n\\r\\f\\v 空格）|
| `strings.pad_left(s, width, fill=" ")` | str,int,str | str | 左填充到 width |
| `strings.pad_right(s, width, fill=" ")` | str,int,str | str | 右填充到 width |
| `strings.center(s, width, fill=" ")` | str,int,str | str | 居中填充 |

### 大小写

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strings.to_upper(s)` | str | str | 全部大写 |
| `strings.to_lower(s)` | str | str | 全部小写 |
| `strings.title(s)` | str | str | 每个单词首字母大写 |
| `strings.capitalize(s)` | str | str | 仅首字母大写 |

### 字符类判断（接受单字符 str）

| 函数 | 返回 | 描述 |
|---|---|---|
| `strings.is_alpha(c)` | bool | 字母（ASCII）|
| `strings.is_digit(c)` | bool | 十进制数字 |
| `strings.is_alnum(c)` | bool | 字母或数字 |
| `strings.is_space(c)` | bool | 空白字符 |
| `strings.is_upper(c)` | bool | 大写字母 |
| `strings.is_lower(c)` | bool | 小写字母 |

### 字节与编码

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `strings.to_bytes(s)` | str | list[int] | UTF-8 字节列表 |
| `strings.from_bytes(list)` | list[int] | str | UTF-8 字节列表 → 字符串 |
| `strings.to_buffer(s)` | str | buffer | 转为 buffer（基于 buffer 模块）|
| `strings.is_empty(s)` | str | bool | `len(s) == 0` |

---

## 依赖

- `_strprim`（C 原语：join/repeat/fields；**亦负责** split_n/replace(old,new,n)/trim(chars)/trim_left/trim_right/last_index/count，因内建方法不支持这些变体）
- 内置字符串方法（`s.split(sep)`/`s.replace(old,new)`/`s.upper()`/`s.lower()`/`s.trim()`/`s.index_of(sub)`/`s.starts_with(prefix)`/`s.ends_with(suffix)` 已在 `vm_builtins.c`）
- 注：`has_prefix`/`has_suffix` 是对 `starts_with`/`ends_with` 的函数式别名；`index` 对应 `index_of`；内建 `replace` 无 n 参、`trim` 无 chars 参，须由 `_strprim` 补全

---

## 示例

```ms
import "strings"

print(strings.join(["a", "b", "c"], "-"))   // a-b-c
print(strings.repeat("ab", 3))              // ababab
print(strings.fields("  foo  bar  "))       // ["foo", "bar"]
print(strings.pad_left("42", 6))            // "    42"
print(strings.title("hello world"))         // Hello World

var s = "hello world"
print(strings.index(s, "world"))            // 6
print(strings.replace(s, "o", "0", 1))     // hell0 world
```

---

## 测试

```
tests/unit/test_stdlib_strings.c
tests/fixtures/stdlib_strings_basic.ms
```
