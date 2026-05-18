# STDLIB-03: time 模块

## 职责

时间获取、格式化/解析、同步睡眠，以及从全局命名空间迁入的异步调度函数（`sleep`、`run_until_complete`、`gather`、`resume`）。

---

## 函数清单

### 时间获取

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `time.now()` | – | num | Unix 时间戳（秒，浮点，含毫秒精度）|
| `time.now_ms()` | – | int | Unix 时间戳毫秒整数 |
| `time.monotonic_ms()` | – | int | 单调时钟毫秒，复用 `ms_monotonic_ms()` |
| `time.clock()` | – | num | 进程 CPU 时间（迁自原全局 `clock`，`clock()/CLOCKS_PER_SEC`）|

### 格式化与解析

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `time.format(ts, fmt)` | num,str | str | `strftime` 包装；`ts` 为 Unix 秒；`fmt` 同 C strftime |
| `time.parse(text, fmt)` | str,str | num | `strptime`（Unix）/ 手动实现（Windows），返回 Unix 秒 |
| `time.struct(ts)` | num | map | 拆解为 `{year, month, day, hour, min, sec, wday, yday, dst}` |

```ms
import time
print(time.format(time.now(), "%Y-%m-%d %H:%M:%S"))
var t = time.struct(time.now())
print(t["year"], t["month"], t["day"])
```

### Windows 子集（strptime 手动实现）

Windows 无 `strptime`，v1 手动实现仅支持以下 conversion specifier：

| 说明符 | 含义 |
|--------|------|
| `%Y`   | 四位年 |
| `%m`   | 月（01–12）|
| `%d`   | 日（01–31）|
| `%H`   | 时（00–23）|
| `%M`   | 分（00–59）|
| `%S`   | 秒（00–60）|
| `%j`   | 年内第几天（001–366）|

- 其他说明符：抛出运行时错误 `time.parse: unsupported specifier '%X' on Windows`。
- 时区：固定 UTC，不处理 DST。
- 解析失败（格式不匹配）：抛出 `time.parse: invalid format`，不返回 nan。

### 睡眠

| 函数 | 参数 | 返回 | async | 描述 |
|---|---|---|---|---|
| `time.sleep(seconds)` | num | Future | ✓ | 迁自原全局 `sleep`；仍返回 Future，配合 `await` |
| `time.sleep_sync(seconds)` | num | nil | – | 真同步阻塞（`usleep`/`Sleep`）|

```ms
import time
async fun main() {
    await time.sleep(1.0)
    print("1 second later")
}
time.run_until_complete(main())
```

### Async 调度（迁自全局）

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `time.run_until_complete(fut)` | Future/Coroutine | any | 迁自 `run_until_complete`；驱动 EventLoop |
| `time.gather(f1, f2, ...)` | Future... | Future | 迁自 `gather`；全部完成后 resolve 为结果列表 |
| `time.resume(coro, val)` | Coroutine,any | any | 迁自 `resume`；手动 resume 生成器协程 |

---

## 实现（`src/stdlib/time.c`）

`sleep` / `run_until_complete` / `gather` / `resume` 的 C 实现直接从 `src/vm_natives.c` 中的 `native_sleep`、`native_run_until_complete`、`native_gather`、`native_resume` **搬移**至此文件，改为 `static` 函数，不改动实现逻辑。

```c
#include "ms/module.h"
#include "ms/event_loop.h"
#include "ms/object.h"
#include <time.h>

static MsValue ms_time_now(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc; (void)argv;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return MS_NUMBER_VAL((double)ts.tv_sec + ts.tv_nsec / 1e9);
}

static MsValue ms_time_format(MsVM* vm, int argc, MsValue* argv) {
    time_t t = (time_t)MS_AS_NUMBER(argv[0]);
    const char* fmt = MS_AS_CSTRING(argv[1]);
    struct tm* tm_info = localtime(&t);
    char buf[256];
    strftime(buf, sizeof(buf), fmt, tm_info);
    return MS_OBJ_VAL(ms_obj_string_copy(vm, buf, (int)strlen(buf)));
}

/* sleep 直接从 vm_natives.c 搬：不变实现，只改作用域 */
static MsValue ms_time_sleep(MsVM* vm, int argc, MsValue* argv) {
    /* ... 与原 native_sleep 相同 ... */
}

static const MsNativeDef ms_time_defs[] = {
    {"now",                ms_time_now,                0},
    {"now_ms",             ms_time_now_ms,             0},
    {"monotonic_ms",       ms_time_monotonic_ms,       0},
    {"clock",              ms_time_clock,              0},
    {"format",             ms_time_format,             2},
    {"parse",              ms_time_parse,              2},
    {"struct",             ms_time_struct,             1},
    {"sleep",              ms_time_sleep,              1},
    {"sleep_sync",         ms_time_sleep_sync,         1},
    {"run_until_complete", ms_time_run_until_complete, 1},
    {"gather",             ms_time_gather,            -1},
    {"resume",             ms_time_resume,             2},
    {NULL, NULL, 0}
};

void ms_module_time_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_time_defs);
}
```

### Windows `time.now()`

```c
#ifdef _WIN32
static MsValue ms_time_now(MsVM* vm, int argc, MsValue* argv) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ul;
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    /* FILETIME epoch 1601-01-01，Unix epoch 1970-01-01，差 116444736000000000 * 100ns */
    double secs = (double)(ul.QuadPart - 116444736000000000ULL) / 1e7;
    return MS_NUMBER_VAL(secs);
}
#endif
```

---

## 依赖

- CAPI-01/02
- `<time.h>`（C99 `clock_gettime` 在 POSIX 下需 `-lrt`，Linux >= 2.17 内置）
- 复用 `ms_monotonic_ms()`（`event_loop.h`）
- 搬移 `native_sleep` / `gather` / `run_until_complete` / `resume` 的实现

---

## 测试

```ms
// tests/fixtures/stdlib_time_basic.ms
import time
var t0 = time.now()
time.sleep_sync(0.01)
var t1 = time.now()
assert(t1 - t0 >= 0.01)
print(time.format(t0, "%Y"))
```

```ms
// tests/fixtures/stdlib_time_async.ms
import time
async fun delayed() {
    await time.sleep(0.05)
    return 42
}
var result = time.run_until_complete(delayed())
assert(result == 42)
```
