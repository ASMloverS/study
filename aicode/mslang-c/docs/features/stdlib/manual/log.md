# log 模块

分级日志，支持自定义 sink / 格式，以及带 tag 的子日志���。

```ms
import "log"
```

> 实现规格：[STDLIB-07-log.md](../STDLIB-07-log.md)

---

## 函数速查表

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `log.trace(fmt, ...)` | str, any... | nil | 输出 TRACE 级别日志 |
| `log.debug(fmt, ...)` | str, any... | nil | 输出 DEBUG 级别日志 |
| `log.info(fmt, ...)` | str, any... | nil | 输出 INFO 级别日志（**默认最低级别**）|
| `log.warn(fmt, ...)` | str, any... | nil | 输出 WARN 级别日志 |
| `log.error(fmt, ...)` | str, any... | nil | 输出 ERROR 级别日志 |
| `log.fatal(fmt, ...)` | str, any... | nil | 输出 FATAL 日志并退出进程（默认）|
| `log.set_level(level)` | str \| int | nil | 设置全局最低输出级别 |
| `log.get_level()` | — | str | 获取当前全局级别字符串 |
| `log.set_sink(f)` | File \| nil | nil | 设置输出目标（nil = 恢复 stderr）|
| `log.set_format(fmt)` | str | nil | 设置行格式模板 |
| `log.set_fatal_exits(b)` | bool | nil | 控制 fatal 是否退出（默认 true）|
| `log.with(tag)` | str | Logger | 创建带 tag 的子日志器 |

### 日志级别

| 整数 | 字符串 | 说明 |
|---|---|---|
| 0 | `"trace"` | 最详细 |
| 1 | `"debug"` | 调试信息 |
| 2 | `"info"` | 一般信息（**默认**）|
| 3 | `"warn"` | 警告 |
| 4 | `"error"` | 错误 |
| 5 | `"fatal"` | 致命错误 |

### Logger 子日志器方法

`log.with(tag)` 返回 Logger 对象，支持以下方法：

| 方法 | 说明 |
|---|---|
| `logger.trace/debug/info/warn/error/fatal(fmt, ...)` | 同全局函数，附加 tag |
| `logger.set_level(level)` | 为此 Logger 单独设置级别（-1 = 继承全局）|

---

## 分组详解

### 基本用法

默认输出到 **stderr**，默认级别 **INFO**（低于 INFO 的消息被过滤）。

```ms
import "log"

log.info("服务启动")            // 输出到 stderr
log.warn("内存使用率 %d%%", 85)
log.error("连接失败: %s", "timeout")
```

消息格式支持 `%s`（字符串）、`%d`（整数）、`%f`（浮点）、`%v`（任意值）占位符：

```ms
import "log"
log.info("用户 %s 登录，ID=%d，时间=%.2f", "alice", 42, 3.14)
// → 用户 alice ���录，ID=42，时间=3.14
```

### 控制输出目标和级别

```ms
import "log"
import "io"

// 输出到 stdout
log.set_sink(io.stdout)

// 也可以输出到文件
var f = io.open("app.log", "a")
log.set_sink(f)

// 恢复 stderr
log.set_sink(nil)

// 调整全局级别
log.set_level("debug")
log.debug("��在可见")   // 低于 INFO 的级别也会显示
```

### 自定义格式

`set_format` 接受包含 `{time}`、`{level}`、`{tag}`、`{msg}` 的模板字符串：

```ms
import "log"
import "io"
log.set_sink(io.stdout)

log.set_format("[{level}] {tag}{msg}")
log.info("hello")      // [INFO ] hello

log.set_format("{time} | {level} | {msg}")
log.warn("disk full")  // 2026-05-23T14:30:00 | WARN  | disk full
```

> 默认格式：`"{time} [{level}] {msg}"`，其中 `{time}` 为 `2026-05-23T14:30:00` 格式（本地时间）。

### 带 tag 的子日���器

```ms
import "log"
import "io"
log.set_sink(io.stdout)
log.set_format("[{level}] [{tag}] {msg}")

var db  = log.with("DB")
var net = log.with("NET")

db.info("连接成功")       // [INFO ] [DB] 连接成功
net.warn("延迟高: %d ms", 200)   // [WARN ] [NET] 延迟高: 200 ms

// 子日志器独立级别
db.set_level("error")
db.warn("被过滤")    // （不输出）
db.error("严重错��") // [ERROR] [DB] 严重错误
```

> **格式注意**：`{tag}` 直接嵌入，不自动添加分隔符。如需 `[tag]` 效果，在 format 中写 `[{tag}]`；如需无 tag 时隐藏，使用 Logger 而不是直接用 `{tag}` 占位符。

### 控制 fatal 行为

```ms
import "log"
log.set_fatal_exits(false)  // fatal 不退出进程
log.fatal("something terrible happened")
print("仍在运行")   // 会被执行
```

---

## 完整示例

文件：[`examples/log.ms`](examples/log.ms)

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/log.ms
INFO
[INFO ] server started
[WARN ] disk usage high
[ERROR] connection lost
[DEBUG] now visible
[INFO ] user alice logged in, age 30
[INFO ] DBquery ok
[WARN ] DBslow query: 120 ms
[ERROR] DBcritical failure
[FATAL] fatal but no exit
still running
```

---

## 实现/性能注解

- log 状态是**全局单例**（C 静态变量），脚本中任何地方调用都影响同一状态，包括 level / sink / format。
- `set_sink` 接受 `io.stdout` / `io.stderr` 等 File 句柄，不接受文件路径字符串。
- `{level}` 固定 5 个字符（含尾部空格），`"INFO "` `"WARN "` 等，对齐列宽。

## 常见陷阱

```ms
import "log"
// ❌ log.fatal 默认直接退出，不执行之后的代码
// log.fatal("oops")
// print("不会到达")   ← 不执行

// ✅ 在测试时先禁用 fatal 退出
log.set_fatal_exits(false)

// ❌ set_sink 接受 File，不接受路径字符串
// log.set_sink("app.log")  → 运行时错误
// ✅ 先 open，再 set_sink
import "io"
var f = io.open("app.log", "a")
log.set_sink(f)

// ❌ {tag} 无自动分隔符
// log.set_format("[{level}] {tag}: {msg}")  → "[INFO ] DB: msg" ✅
// log.set_format("[{level}] {tag}{msg}")    → "[INFO ] DBmsg"   ← 无空格
```
