import "gc"

// pause / is_paused / resume
assert(!gc.is_paused())
gc.pause()
assert(gc.is_paused())
gc.resume()
assert(!gc.is_paused())

// set_threshold: clamps to >= 1024
gc.set_threshold(100)
assert(gc.threshold() == 1024)

// set_threshold: exact value above minimum
gc.set_threshold(1024 * 1024)
assert(gc.threshold() == 1024 * 1024)

// threshold() reflects last set value
gc.set_threshold(8192)
assert(gc.threshold() == 8192)

print("ok")
// expect: ok
