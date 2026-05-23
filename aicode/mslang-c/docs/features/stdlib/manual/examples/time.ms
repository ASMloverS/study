import "time"

// 当前时间（Unix 时间戳，秒）
var ts = time.now()
print(ts > 0)          // true

// 毫秒级时间戳
var ms = time.now_ms()
print(ms > 0)          // true

// 单调时钟（用于计时）
var t0 = time.monotonic_ms()
print(t0 > 0)          // true

// 格式化时间戳为字符串（strftime 格式）
var formatted = time.format(ts, "%Y-%m-%d")
print(len(formatted) == 10)   // true（如 "2026-05-23"）

// 解析时间字符串为 Unix 时间戳
var parsed = time.parse("2026-01-01", "%Y-%m-%d")
print(parsed > 0)      // true

// 分解时间戳为结构体
var s = time.struct(parsed)
print(s["year"])    // 2026
print(s["month"])   // 1
print(s["day"])     // 1

// 同步睡眠（0.01 秒，验证 monotonic_ms 变化）
var t1 = time.monotonic_ms()
time.sleep_sync(0.01)
var t2 = time.monotonic_ms()
print(t2 >= t1)   // true

// CPU 时钟时间
var cpu = time.clock()
print(cpu >= 0)   // true

// 异步：用 run_until_complete 驱动一个简单的 async 函数
async fun double(x) {
    return x * 2
}
var result = time.run_until_complete(double(21))
print(result)   // 42
