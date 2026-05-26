import "bytes"

// new
var b0 = bytes.new()
print(b0.len())
// expect: 0

var b1 = bytes.new(3, 65)
print(b1.len())
// expect: 3
print(b1.get(0))
// expect: 65

// from_str
var hello = bytes.from_str("Hello")
print(hello.len())
// expect: 5

// from_list
var nums = bytes.from_list([72, 101, 108, 108, 111])
print(bytes.to_str(nums))
// expect: Hello

// copy
var orig = bytes.from_str("hi")
var cp   = bytes.copy(orig)
cp.set(0, 88)
print(orig.get(0))
// expect: 104
print(cp.get(0))
// expect: 88

// concat
var a = bytes.from_str("Hello")
var b = bytes.from_str(" World")
var joined = bytes.concat(a, b)
print(bytes.to_str(joined))
// expect: Hello World

// repeat
var repeated = bytes.repeat(bytes.from_str("ab"), 3)
print(bytes.to_str(repeated))
// expect: ababab

// to_list
var lst = bytes.to_list(bytes.from_str("Hi"))
print(lst[0])
// expect: 72
print(lst[1])
// expect: 105

// equal
print(bytes.equal(bytes.from_str("Hello"), bytes.from_str("Hello")))
// expect: true
print(bytes.equal(bytes.from_str("Hello"), bytes.from_str("World")))
// expect: false

// compare
print(bytes.compare(bytes.from_str("abc"), bytes.from_str("abc")))
// expect: 0
print(bytes.compare(bytes.from_str("abc"), bytes.from_str("abd")))
// expect: -1
print(bytes.compare(bytes.from_str("abd"), bytes.from_str("abc")))
// expect: 1

// contains
var hay = bytes.from_str("Hello World")
print(bytes.contains(hay, bytes.from_str("World")))
// expect: true
print(bytes.contains(hay, bytes.from_str("xyz")))
// expect: false

// index
print(bytes.index(hay, bytes.from_str("World")))
// expect: 6
print(bytes.index(hay, bytes.from_str("xyz")))
// expect: -1

// count
print(bytes.count(bytes.from_str("ababab"), bytes.from_str("ab")))
// expect: 3

// slice
var sliced = bytes.slice(hay, 0, 5)
print(bytes.to_str(sliced))
// expect: Hello

// trim
var padded = bytes.from_str("  hello  ")
var space  = bytes.from_list([32])
print(bytes.to_str(bytes.trim(padded, space)))
// expect: hello

// replace
var rp = bytes.replace(bytes.from_str("ababab"), bytes.from_str("ab"), bytes.from_str("X"))
print(bytes.to_str(rp))
// expect: XXX

// reverse
var rev = bytes.reverse(bytes.from_str("Hello"))
print(bytes.to_str(rev))
// expect: olleH
