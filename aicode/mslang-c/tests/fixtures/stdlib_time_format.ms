import "time"

// format: fixed timestamp 2021-01-01 00:00:00 UTC
var ts = 1609459200.0
var s = time.format(ts, "%Y-%m-%d")
assert(type(s) == "string")
assert(len(s) == 10)

// format: year only is 4 chars
var y = time.format(0.0, "%Y")
assert(type(y) == "string")
assert(len(y) == 4)

// parse roundtrip
var fmt = "%Y-%m-%d"
var t2 = time.parse(s, fmt)
assert(type(t2) == "number")

// struct: known fields from 2021-01-01
var st = time.struct(ts)
assert(type(st) == "map")
assert(st["year"]  == 2021)
assert(st["month"] == 1)
assert(st["day"]   == 1)

// struct: all keys present
var st2 = time.struct(time.now())
var _h = st2["hour"]
var _m = st2["min"]
var _s = st2["sec"]
var _w = st2["wday"]
var _y = st2["yday"]

print("ok")
// expect: ok
