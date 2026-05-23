import "log"

// fatal must not exit process in test context
log.set_fatal_exits(false)

// set_level / get_level with int
log.set_level(3)
assert(log.get_level() == "WARN")

// set_level / get_level with string
log.set_level("error")
assert(log.get_level() == "ERROR")

// reset to INFO (level 2) for remaining tests
log.set_level(2)

// all levels callable at trace (nothing crashes)
log.set_level("trace")
log.trace("t")
log.debug("d")
log.info("i")
log.warn("w")
log.error("e")
log.fatal("f")
log.set_level(2)

// messages below level are silently dropped
log.set_level("warn")
log.trace("hidden")
log.debug("hidden")
log.info("hidden")
log.set_level(2)

print("ok")
// expect: ok
