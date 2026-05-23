import "gc"

// collect returns non-negative int (freed object count)
var n = gc.collect()
assert(n >= 0)

// minor returns non-negative int
var m = gc.minor()
assert(m >= 0)

// bytes_allocated and alive are non-negative
assert(gc.bytes_allocated() >= 0)
assert(gc.alive() >= 0)

// object_counts returns a non-nil map
var oc = gc.object_counts()
assert(oc != nil)

print("ok")
// expect: ok
