import "buffer"

// slice
var b = buffer.from_str("hello world")
var s = b.slice(0, 5)
assert(s.to_str() == "hello")

var b2 = buffer.from_str("hello")
var s2 = b2.slice(2, -1)
assert(s2.to_str() == "llo")

// append (str)
var ba = buffer.from_str("hello")
ba.append(" world")
assert(ba.len() == 11)
assert(ba.to_str() == "hello world")

// append (buffer)
var ba2 = buffer.from_str("foo")
var bb2 = buffer.from_str("bar")
ba2.append(bb2)
assert(ba2.to_str() == "foobar")

// prepend
var bp = buffer.from_str("world")
bp.prepend("hello ")
assert(bp.to_str() == "hello world")

// fill
var bf = buffer.new(4, 0)
bf.fill(0xAA, 1, 3)
assert(bf.get(0) == 0)
assert(bf.get(1) == 0xAA)
assert(bf.get(2) == 0xAA)
assert(bf.get(3) == 0)

// clear
var bc = buffer.from_str("hello")
bc.clear()
assert(bc.len() == 0)

// resize grow with fill
var br = buffer.from_str("ab")
br.resize(5, 0xCC)
assert(br.len() == 5)
assert(br.get(2) == 0xCC)
assert(br.get(4) == 0xCC)

// resize shrink
var br2 = buffer.from_str("hello")
br2.resize(2)
assert(br2.len() == 2)
assert(br2.to_str() == "he")

// copy independence
var orig = buffer.from_str("hello")
var cp = orig.copy()
cp.set(0, 88)  // 'X'
assert(orig.get(0) == 104)  // unchanged
assert(cp.get(0) == 88)

// concat (method)
var cm1 = buffer.from_str("foo")
var cm2 = buffer.from_str("bar")
var cm3 = cm1.concat(cm2)
assert(cm3.to_str() == "foobar")

print("ok")
// expect: ok
