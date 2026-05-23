# time 模块

时间获取、格式化解析、同步睡眠，以及异步事件循环驱动。

```ms
import "time"
```

> 实现规格：[STDLIB-03-time.md](../STDLIB-03-time.md)

---

## 函数速查表

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `time.now()` | — | num | Unix 时间戳（秒，浮点） |
| `time.now_ms()` | — | int | Unix 时间戳（毫秒，整数） |
| `time.monotonic_ms()` | — | int | 单调时钟（毫秒，用于计时）|
| `time.clock()` | — | num | 进程 CPU 时间（秒）|
| `time.format(ts, fmt)` | num, str | str | 格式化时间戳（strftime 格式）|
| `time.parse(text, fmt)` | str, str | num | 解析时间字符串为 Unix 时间戳 |
| `time.struct(ts)` | num | map | 分解为本地时间结构体 |
| `time.sleep_sync(secs)` | num | nil | 同步睡眠（阻塞，单位：秒）|
| `time.sleep(ms)` | num | Future | 异步睡眠（单位：**毫秒**，返回 Future）|
| `time.run_until_complete(f)` | Future | any | 驱动事件循环直到 Future 完成 |
| `time.gather(list)` | list[Future] | Future | 并发等待多个 Future，返回结果列表 |
| `time.resume(co[, val])` | Coroutine, any? | any | 恢复协程，传入可选值 |

---

## 分组详解

### 时间获取

```ms
import "time"
var ts = time.now()         // 1748000000.123 （Unix 时间戳，秒）
var ms = time.now_ms()      // 1748000000123  （毫秒）
var mono = time.monotonic_ms()  // 单调时钟，不受系统时间调整影响
var cpu = time.clock()      // 进程累计 CPU 时间（秒）
```

### 格式化与解析

`time.format` 使用 C `strftime` 格式字符串：

```ms
import "time"
var ts = time.now()
print(time.format(ts, "%Y-%m-%d"))         // 2026-05-23
print(time.format(ts, "%H:%M:%S"))         // 14:32:05
print(time.format(ts, "%Y/%m/%d %H:%M"))   // 2026/05/23 14:32
```

`time.parse` 解析字符串为 Unix 时间戳（double）：

> **Windows 限制**：`time.parse` 仅支持 `%Y %m %d %H %M %S %j`；其他 `strftime` 格式符在 Windows 上会报错。

```ms
import "time"
var t = time.parse("2026-01-01", "%Y-%m-%d")
print(time.format(t, "%Y"))   // 2026
```

### 分解时间结构体

`time.struct(ts)` 将 Unix 时间戳分解为本地时间 map：

| 字段 | 说明 |
|---|---|
| `"year"` | 年（如 2026） |
| `"month"` | 月（1–12）|
| `"day"` | 日（1–31）|
| `"hour"` | 时（0–23）|
| `"min"` | 分（0–59）|
| `"sec"` | 秒（0–59）|
| `"wday"` | 星期（0=周日…6=周六）|
| `"yday"` | 一年中第几天（0–365）|
| `"dst"` | 夏令时标志（正/负/0）|

```ms
import "time"
var s = time.struct(time.now())
print(s["year"])    // 2026
print(s["month"])   // 5
print(s["wday"])    // 0=周日, 1=周一, ...
```

### 同步睡眠

```ms
import "time"
var t0 = time.monotonic_ms()
time.sleep_sync(0.1)          // 睡 100 ms
var t1 = time.monotonic_ms()
print(t1 - t0 >= 100)         // true
```

### 异步事件循环

`time.sleep(ms)` 返回 Future，需要在 `async` 函数里 `await`，或用 `run_until_complete` 驱动：

```ms
import "time"

async fun delayed_sum(a, b) {
    await time.sleep(10)   // 异步睡 10ms
    return a + b
}

var result = time.run_until_complete(delayed_sum(3, 4))
print(result)   // 7
```

并发等待多个 Future：

```ms
import "time"

async fun slow(x) {
    await time.sleep(5)
    return x * x
}

var fut = time.gather([slow(2), slow(3), slow(4)])
var results = time.run_until_complete(fut)
print(results)   // [4, 9, 16]
```

---

## 完整示例

文件：[`examples/time.ms`](examples/time.ms)

```ms
import "time"

var ts = time.now()
print(ts > 0)

var ms = time.now_ms()
print(ms > 0)

var t0 = time.monotonic_ms()
print(t0 > 0)

var formatted = time.format(ts, "%Y-%m-%d")
print(len(formatted) == 10)

var parsed = time.parse("2026-01-01", "%Y-%m-%d")
print(parsed > 0)

var s = time.struct(parsed)
print(s["year"])
print(s["month"])
print(s["day"])

var t1 = time.monotonic_ms()
time.sleep_sync(0.01)
var t2 = time.monotonic_ms()
print(t2 >= t1)

var cpu = time.clock()
print(cpu >= 0)

async fun double(x) {
    return x * 2
}
var result = time.run_until_complete(double(21))
print(result)
```

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/time.ms
true
true
true
true
true
2026
1
1
true
true
42
```

---

## 实现/性能注解

- `time.now()` 在 Windows 使用 `GetSystemTimeAsFileTime`，在 Unix 使用 `clock_gettime(CLOCK_REALTIME)`。
- `time.monotonic_ms()` 不受系统时间跳变影响，推荐用于性能计时（非 `now()`）。
- `time.sleep(ms)` 的单位是**毫秒**，而 `sleep_sync(secs)` 的单位是**秒**，两者不同。

## 常见陷阱

```ms
import "time"
// ❌ sleep 和 sleep_sync 单位不同
// time.sleep(1)       → 异步睡 1 毫秒
// time.sleep_sync(1)  → 同步睡 1 秒

// ❌ sleep 返回 Future，直接 print 不会等待
// var f = time.sleep(100)
// print("done")   ← 立即打印，Future 还未完成

// ✅ 在 async 函数里 await，或用 run_until_complete 驱动
async fun example() { await time.sleep(100) }
time.run_until_complete(example())
```
