import "log"
import "io"

// 重定向输出到 stdout（默认是 stderr）
log.set_sink(io.stdout)

// 设置无时间戳的格式，方便示例输出可重现
log.set_format("[{level}] {tag}{msg}")

// 默认级别 INFO（级别 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=FATAL）
print(log.get_level())   // INFO

// 低于当前级别的消息被过滤
log.trace("this is trace")   // （不输出，低于 INFO）
log.debug("this is debug")   // （不输出，低于 INFO）
log.info("server started")   // [INFO ] server started
log.warn("disk usage high")  // [WARN ] disk usage high
log.error("connection lost") // [ERROR] connection lost

// 调整全局级别
log.set_level("debug")
log.debug("now visible")     // [DEBUG] now visible

// 格式化消息（使用 %s %d %f 插值）
log.info("user %s logged in, age %d", "alice", 30)
// [INFO ] user alice logged in, age 30

// 带标签的子日志器
var db = log.with("DB")
db.info("query ok")          // [INFO ] DBquery ok
db.warn("slow query: %d ms", 120)  // [WARN ] DBslow query: 120 ms

// 子日志器可以设置独立级别
db.set_level("error")
db.warn("this is filtered")  // （不输出，DB 的级别为 ERROR）
db.error("critical failure") // [ERROR] DBcritical failure

// 禁止 fatal 退出进程（安全演示）
log.set_fatal_exits(false)
log.fatal("fatal but no exit")  // [FATAL] fatal but no exit
print("still running")          // still running
