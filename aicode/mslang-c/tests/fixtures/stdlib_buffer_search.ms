import "buffer"

// find: found
var b = buffer.from_str("hello world")
var sub = buffer.from_str("world")
assert(b.find(sub) == 6)

// find: not found returns -1
var b2 = buffer.from_str("hello")
var sub2 = buffer.from_str("xyz")
assert(b2.find(sub2) == -1)

// find: string arg
var b3 = buffer.from_str("hello world")
assert(b3.find("world") == 6)

// find: with start offset
var b4 = buffer.from_str("aababc")
assert(b4.find("ab", 2) == 3)

// replace: all occurrences
var b5 = buffer.from_str("aababab")
var old = buffer.from_str("ab")
var nw  = buffer.from_str("X")
var r   = b5.replace(old, nw)
assert(r.to_str() == "aXXX")

// replace: count limit
var b6  = buffer.from_str("ababab")
var old2 = buffer.from_str("ab")
var nw2  = buffer.from_str("X")
var r2   = b6.replace(old2, nw2, 2)
assert(r2.to_str() == "XXab")

// equals: true
var ea = buffer.from_str("hello")
var eb = buffer.from_str("hello")
assert(ea.equals(eb) == true)

// equals: false
var ec = buffer.from_str("hello")
var ed = buffer.from_str("world")
assert(ec.equals(ed) == false)

print("ok")
// expect: ok
