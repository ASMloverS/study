import "time"

// now() returns a positive float past year 2001
var t = time.now()
assert(type(t) == "number")
assert(t > 1000000000.0)

// now_ms() returns int > 0
var ms = time.now_ms()
assert(type(ms) == "int")
assert(ms > 1000000000000)

// monotonic_ms(): non-negative int, non-decreasing
var m1 = time.monotonic_ms()
assert(type(m1) == "int")
assert(m1 >= 0)
var m2 = time.monotonic_ms()
assert(m2 >= m1)

// clock(): non-negative float
var c = time.clock()
assert(type(c) == "number")
assert(c >= 0.0)

// sleep_sync: at least some time elapses
var t0 = time.monotonic_ms()
time.sleep_sync(0.01)
var t1 = time.monotonic_ms()
assert(t1 - t0 >= 5)

print("ok")
// expect: ok
