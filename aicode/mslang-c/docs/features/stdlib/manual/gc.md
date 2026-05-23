# gc 模块

手动控制垃圾回收器，查询内存统计与对象存活情况。

```ms
import "gc"
```

> 实现规格：[STDLIB-10-gc.md](../STDLIB-10-gc.md)

---

## 函数速查表

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `gc.collect()` | — | int | 触发完整 GC，返回释放的对象数 |
| `gc.minor()` | — | int | 触发次代（年轻代）GC，返回释放对象数 |
| `gc.alive()` | — | int | 当前存活对象总数 |
| `gc.bytes_allocated()` | — | int | VM 当前分配的内存字节数 |
| `gc.threshold()` | — | int | 触发 GC 的内存阈值（字节）|
| `gc.object_counts()` | — | map | 各类型对象的存活数量（类型名 → 数量）|
| `gc.set_threshold(n)` | int | nil | 设置 GC 阈值（最小 1024）|
| `gc.pause()` | — | nil | 暂停自动 GC（手动 `collect` 仍可用）|
| `gc.resume()` | — | nil | 恢复自动 GC |
| `gc.is_paused()` | — | bool | 是否已暂停 |

---

## 分组详解

### 统计信息

```ms
import "gc"
print(gc.alive())             // 当前存活对象数
print(gc.bytes_allocated())   // 已分配内存字节
print(gc.threshold())         // GC 触发阈值

// 各类型对象计数
var counts = gc.object_counts()
// map 的键：string/function/closure/list/map/tuple/...
```

`object_counts()` 的键对应对象类型名称：

| 键 | 说明 |
|---|---|
| `"string"` | 字符串 |
| `"list"` | 列表 |
| `"map"` | 映射 |
| `"tuple"` | 元组 |
| `"closure"` | 脚本函数 / 闭包 |
| `"coroutine"` | 协程 |
| `"future"` | Future 对象 |
| `"socket"` | 网络 Socket |
| `"buffer"` | Buffer |
| `"userdata"` | 用户数据 |
| ... | 其余类型见实现 |

### 手动 GC

```ms
import "gc"

// 完整 GC（扫描全部代）
var freed = gc.collect()
print(freed)   // 释放的对象数（>= 0）

// 次代 GC（仅扫描年轻代，更快）
var freed2 = gc.minor()
print(freed2)
```

### 调整阈值

```ms
import "gc"
var old = gc.threshold()
print(old)   // 默认阈值（通常 1 MB 左右）

gc.set_threshold(old * 2)   // 延迟 GC 触发
// ... 大量分配 ...
gc.set_threshold(old)       // 恢复默认
```

### 暂停 / 恢复 GC

适用于需要确定性延迟（如实时循环、基准测试）的场景：

```ms
import "gc"

gc.pause()
// 在此区间内不会触发自动 GC
var results = []
var i = 0
while (i < 10000) {
    results.push(i * i)
    i = i + 1
}
gc.resume()   // 恢复自动 GC，可能立即触发一次
```

---

## 完整示例

文件：[`examples/gc.ms`](examples/gc.ms)

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/gc.ms
true
true
true
true
true
true
true
true
false
true
true
```

---

## 实现/性能注解

- mslang 使用三代 GC（年轻/老/全），`collect()` 扫描所有代，`minor()` 仅扫描年轻代。
- `gc.pause()` 只禁用**自动触发**，不影响手动调用 `gc.collect()`。
- `set_threshold(n)` 的最小值为 1024，传入更小的值会被忽略并设为 1024。
- `bytes_allocated()` 包含 GC 跟踪的所有对象头和数据，不包含 C 运行时的其他分配。

## 常见陷阱

```ms
import "gc"
// ❌ pause 后不能无限堆积内存——只是禁用了自动触发
// gc.pause()
// ... 无限分配 ...
// ✅ 适时 collect 或限制 pause 的作用范围

// ❌ set_threshold 小于 1024 不会生效
gc.set_threshold(100)
print(gc.threshold())  // 仍然是 1024

// ❌ gc.collect() 返回的是释放数，不是存活数
var freed = gc.collect()
// 不要用 freed 来判断内存是否"足够"
// ✅ 用 gc.alive() 或 gc.bytes_allocated() 判断当前状态
```
