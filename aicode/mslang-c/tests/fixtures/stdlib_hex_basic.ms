import "hex"
import "buffer"

// encode lowercase
print(hex.encode("Hello"))
// expect: 48656c6c6f

// encode uppercase
print(hex.encode_upper("Hello"))
// expect: 48656C6C6F

// empty
print(hex.encode(""))
// expect:

// decode
var buf = hex.decode("48656c6c6f")
print(buf.to_str())
// expect: Hello

// decode empty
var empty = hex.decode("")
print(empty.len())
// expect: 0

// round-trip
var enc = hex.encode("Hello, World!")
var dec = hex.decode(enc)
print(dec.to_str())
// expect: Hello, World!

// buffer input
var b = buffer.from_str("Hi")
print(hex.encode(b))
// expect: 4869

// is_valid
print(hex.is_valid("48656c6c6f"))
// expect: true
print(hex.is_valid("48656g6c6f"))
// expect: false
print(hex.is_valid("abc"))
// expect: false
print(hex.is_valid(""))
// expect: true

// byte_to_hex
print(hex.byte_to_hex(255))
// expect: ff
print(hex.byte_to_hex(0))
// expect: 00
print(hex.byte_to_hex(16))
// expect: 10

// hex_to_byte
print(hex.hex_to_byte("ff"))
// expect: 255
print(hex.hex_to_byte("00"))
// expect: 0
print(hex.hex_to_byte("FF"))
// expect: 255
