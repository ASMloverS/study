# STDLIB-27: bytes 模块

## 职责

高层字节操作 API，对 `buffer` 模块的面向对象包装与补充：
构造、比较、编码转换、子序列查找。对应 Go 的 `bytes` 包。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/bytes.ms`）。所有底层操作委托 `buffer` 模块。

---

## 函数清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bytes.new(n=0, fill=0)` | int, int | buffer | n 字节，全填 fill（0–255）|
| `bytes.from_str(s, encoding='utf8')` | str, str | buffer | 字符串 → 字节（仅支持 `'utf8'`）|
| `bytes.from_list(ints)` | list[int] | buffer | 整数列表 → 字节（每个 0–255）|
| `bytes.copy(buf)` | buffer | buffer | 浅复制 |

### 组合

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bytes.concat(buf1, buf2, ...)` | buffer... | buffer | 拼接多段字节 |
| `bytes.repeat(buf, n)` | buffer, int | buffer | 重复 n 次 |

### 转换

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bytes.to_str(buf, encoding='utf8')` | buffer, str | str | 字节 → 字符串（UTF-8 验证）|
| `bytes.to_list(buf)` | buffer | list[int] | 字节列表（0–255）|

### 比较与查找

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bytes.equal(buf1, buf2)` | buffer, buffer | bool | 字节级等值比较 |
| `bytes.compare(buf1, buf2)` | buffer, buffer | int | -1/0/1 字典序比较 |
| `bytes.contains(haystack, needle)` | buffer, buffer | bool | 是否含子序列 |
| `bytes.index(haystack, needle)` | buffer, buffer | int | 第一次出现位置（-1 未找到）|
| `bytes.count(buf, sub)` | buffer, buffer | int | 不重叠出现次数 |

### 变换

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bytes.slice(buf, start, end=-1)` | buffer,int,int | buffer | 子切片（复制）|
| `bytes.trim(buf, byte_set)` | buffer, buffer | buffer | 两端删除 byte_set 中的字节 |
| `bytes.replace(buf, old, new)` | buffer,buffer,buffer | buffer | 替换所有出现 |
| `bytes.reverse(buf)` | buffer | buffer | 翻转字节顺序（副本）|

---

## 依赖

- `buffer` 模块（STDLIB-05 ✅）

---

## 示例

```ms
import "bytes"

var b1 = bytes.from_str("Hello")
var b2 = bytes.from_str(" World")
var joined = bytes.concat(b1, b2)

print(bytes.to_str(joined))     // Hello World
print(bytes.equal(b1, bytes.from_str("Hello")))  // true
print(bytes.index(joined, bytes.from_str("World")))  // 6

var nums = bytes.from_list([72, 101, 108, 108, 111])
print(bytes.to_str(nums))       // Hello

var repeated = bytes.repeat(bytes.from_str("ab"), 3)
print(bytes.to_str(repeated))   // ababab
```

---

## 测试

```
tests/unit/test_stdlib_bytes.c
tests/fixtures/stdlib_bytes_basic.ms
```
