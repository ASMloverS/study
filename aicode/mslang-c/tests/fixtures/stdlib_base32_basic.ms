import "base32"
import "buffer"

// encode standard
var s = base32.encode("Hello")
print(s)
// expect: JBSWY3DP

// decode round-trip
var buf = base32.decode("JBSWY3DP")
print(buf.to_str())
// expect: Hello

// empty
print(base32.encode(""))
// expect:

// RFC vectors: 1 byte
print(base32.encode("f"))
// expect: MY======

// RFC vectors: 2 bytes
print(base32.encode("fo"))
// expect: MZXQ====

// encode_hex
print(base32.encode_hex("f"))
// expect: CO======

// decode_hex round-trip
print(base32.decode_hex("CO======").to_str())
// expect: f

// encode_raw (no padding)
print(base32.encode_raw("f"))
// expect: MY
print(base32.encode_raw("fo"))
// expect: MZXQ

// is_valid
print(base32.is_valid("JBSWY3DP"))
// expect: true
print(base32.is_valid("bad$"))
// expect: false

// buffer input
var buf2 = buffer.from_str("Hello")
print(base32.encode(buf2))
// expect: JBSWY3DP
