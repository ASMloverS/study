# buffer 模块

可变字节缓冲区（`Buffer`），用于二进制数据处理。

```ms
import "buffer"
```

> 实现规格：[STDLIB-05-buffer.md](../STDLIB-05-buffer.md)

---

## 模块函数

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `buffer.new([size[, fill]])` | int?, int? | Buffer | 创建指定大小的缓冲区，fill 为填充字节（默认 0） |
| `buffer.from_str(s)` | str | Buffer | 把字符串的 UTF-8 字节复制为 Buffer |
| `buffer.from_hex(hex)` | str | Buffer | 从十六进制字符串解码（允许空格分隔） |
| `buffer.concat(a, b)` | Buffer, Buffer | Buffer | 将两个 Buffer 拼接为新 Buffer（静态形式） |

## Buffer 实例方法

| 方法 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `b.len()` | — | int | 当前字节数 |
| `b.cap()` | — | int | 已分配容量 |
| `b.get(i)` | int | int | 读取第 i 个字节（0–255），支持负索引 |
| `b.set(i, v)` | int, int | nil | 写入第 i 个字节，仅取低 8 位 |
| `b.slice(start[, end])` | int, int? | Buffer | 返回 [start, end) 的新 Buffer，end 默认为末尾 |
| `b.append(x)` | str \| Buffer | nil | 追加字节到末尾 |
| `b.prepend(x)` | str \| Buffer | nil | 在头部插入字节 |
| `b.fill(v[, start[, end]])` | int, int?, int? | nil | 将 [start, end) 区间填充为字节 v |
| `b.clear()` | — | nil | 将长度置为 0（不释放内存） |
| `b.resize(n[, fill])` | int, int? | nil | 扩大至 n 字节（新增字节填 fill），或截断 |
| `b.copy()` | — | Buffer | 返回深拷贝 |
| `b.to_str()` | — | str | 将字节解释为 UTF-8 字符串 |
| `b.to_hex()` | — | str | 编码为小写十六进制字符串 |
| `b.find(sub[, start])` | str \| Buffer, int? | int | 查找子序列，返回偏移量，未找到返回 -1 |
| `b.replace(old, new[, count])` | Buffer, Buffer, int? | Buffer | 替换最多 count 次出现（-1 = 全部），返回新 Buffer |
| `b.equals(x)` | Buffer | bool | 逐字节比较是否相等 |
| `b.concat(other)` | Buffer | Buffer | 拼接并返回新 Buffer（方法形式） |

---

## 分组详解

### 创建

```ms
import "buffer"
var b1 = buffer.new()           // 空缓冲区
var b2 = buffer.new(8)          // 8 字节，初始值 0
var b3 = buffer.new(4, 0xFF)    // 4 字节，初始值 255
var b4 = buffer.from_str("Hi")  // 从字符串
var b5 = buffer.from_hex("deadbeef")  // 从十六进制
```

### 访问与修改

```ms
import "buffer"
var b = buffer.from_str("ABC")
print(b.get(0))        // 65  (b'A')
b.set(0, 97)           // 修改为 b'a'
print(b.to_str())      // aBC
print(b.slice(1, 3))   // 返回包含 "BC" 的新 Buffer
```

### 追加与拼接

```ms
import "buffer"
var b = buffer.from_str("Hello")
b.append(", ")
b.append(buffer.from_str("World"))
print(b.to_str())          // Hello, World
print(b.len())             // 12

var merged = buffer.concat(
    buffer.from_str("foo"),
    buffer.from_str("bar")
)
print(merged.to_str())     // foobar
```

### 查找与替换

```ms
import "buffer"
var b = buffer.from_str("abcabc")
print(b.find(buffer.from_str("bc")))                     // 1
var r = b.replace(buffer.from_str("a"), buffer.from_str("X"))
print(r.to_str())                                        // XbcXbc
```

### 十六进制往返

```ms
import "buffer"
var b = buffer.from_str("mslang")
var hex = b.to_hex()
print(hex)                                // 6d736c616e67
print(buffer.from_hex(hex).to_str())      // mslang
```

---

## 完整示例

文件：[`examples/buffer.ms`](examples/buffer.ms)

```ms
import "buffer"

var b = buffer.new(4, 0)
print(b.len())
print(b.get(0))

var b2 = buffer.from_str("Hello")
print(b2.len())
print(b2.to_str())
print(b2.to_hex())

var b3 = buffer.from_hex("deadbeef")
print(b3.len())
print(b3.get(0))

var b4 = buffer.from_str("Foo")
b4.append(" Bar")
print(b4.to_str())

var b5 = buffer.from_str("Hello, World")
var s = b5.slice(7, 12)
print(s.to_str())

print(b5.find(buffer.from_str("World")))
print(b5.find(buffer.from_str("xyz")))

var a = buffer.from_str("AB")
var bb = buffer.from_str("CD")
var c = buffer.concat(a, bb)
print(c.to_str())

print(a.equals(buffer.from_str("AB")))
print(a.equals(buffer.from_str("XY")))

var orig = buffer.from_str("mslang")
var hex = orig.to_hex()
print(hex)
var back = buffer.from_hex(hex)
print(back.to_str())
```

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/buffer.ms
4
0
5
Hello
48656c6c6f
4
222
Foo Bar
World
7
-1
ABCD
true
false
6d736c616e67
mslang
```

---

## 实现/性能注解

- Buffer 内部使用动态数组，`cap()` 是预分配容量，`len()` 是实际字节数。
- `append` / `prepend` 会按需扩容（2× 策略），不会逐字节重分配。
- `replace` 返回新 Buffer，不修改原对象。

## 常见陷阱

```ms
import "buffer"
// ❌ find 的第一个参数必须是 Buffer 或 str，不能是裸数字
// b.find(65)  → 运行时错误

// ✅ 用 from_str 或 new + set 构造单字节 Buffer
var b = buffer.from_str("Hello")
var needle = buffer.new(1)
needle.set(0, 72)   // 'H'
print(b.find(needle))   // 0

// ❌ replace 的 old/new 参数必须都是 Buffer，不能是 str
// b.replace("l", "L")  → 运行时错误
// ✅ 用 buffer.from_str 包装
var r = b.replace(buffer.from_str("l"), buffer.from_str("L"))
print(r.to_str())   // HeLLo
```
