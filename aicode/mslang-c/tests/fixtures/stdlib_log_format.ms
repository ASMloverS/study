import "log"

log.set_fatal_exits(false)

// set_format: custom template
log.set_format("{level}: {msg}")
log.info("formatted")
log.set_format("{time} [{level}] {msg}")  // reset to default

// format specifiers
log.info("hello %s", "world")
log.info("count=%d", 42)
log.info("pi=%f", 3.14)
log.info("val=%v", true)

// set_sink(nil) resets to stderr
log.set_sink(nil)
log.info("back to stderr")

// with() creates a sub-logger
var lg = log.with("myapp")
assert(lg != nil)
lg.info("tagged message")

// sub-logger has set_level
lg.set_level("error")
lg.debug("silent")
lg.error("visible")

// sub-logger all levels
log.set_level("trace")
var lg2 = log.with("t")
lg2.trace("a")
lg2.debug("b")
lg2.info("c")
lg2.warn("d")
lg2.error("e")
lg2.fatal("f")
log.set_level(2)

print("ok")
// expect: ok
