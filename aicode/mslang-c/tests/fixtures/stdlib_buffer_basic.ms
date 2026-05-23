import "buffer"

// new: empty
var b0 = buffer.new()
assert(b0.len() == 0)
assert(b0.cap() == 0)

// new: with size and fill
var b1 = buffer.new(4, 0xff)
assert(b1.len() == 4)
assert(b1.get(0) == 255)
assert(b1.get(3) == 255)

// from_str: byte values
var b2 = buffer.from_str("hello")
assert(b2.len() == 5)
assert(b2.get(0) == 104)  // 'h'
assert(b2.get(4) == 111)  // 'o'

// from_hex: basic decode
var b3 = buffer.from_hex("deadbeef")
assert(b3.len() == 4)
assert(b3.get(0) == 0xde)
assert(b3.get(1) == 0xad)
assert(b3.get(2) == 0xbe)
assert(b3.get(3) == 0xef)

// from_hex: uppercase
var b4 = buffer.from_hex("DEADBEEF")
assert(b4.get(0) == 0xde)

// get with negative index
var b5 = buffer.from_str("abc")
assert(b5.get(-1) == 99)   // 'c'
assert(b5.get(-2) == 98)   // 'b'
assert(b5.get(-3) == 97)   // 'a'

// get out-of-bounds returns nil
assert(b5.get(5)  == nil)
assert(b5.get(-5) == nil)

// set with negative index
var b6 = buffer.from_str("abc")
b6.set(-1, 88)  // 'X'
assert(b6.get(2) == 88)

// to_str / to_hex
var b7 = buffer.from_str("hello")
assert(b7.to_hex() == "68656c6c6f")
assert(b7.to_str() == "hello")

// concat (static)
var a = buffer.from_str("foo")
var bx = buffer.from_str("bar")
var c = buffer.concat(a, bx)
assert(c.len() == 6)
assert(c.to_str() == "foobar")

print("ok")
// expect: ok
