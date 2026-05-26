import "base64"
import "buffer"

// encode
var s = base64.encode("Hello, World!")
print(s)
// expect: SGVsbG8sIFdvcmxkIQ==

// decode round-trip
var buf = base64.decode("SGVsbG8sIFdvcmxkIQ==")
print(buf.to_str())
// expect: Hello, World!

// empty
print(base64.encode(""))
// expect:

// padding 1 byte
print(base64.encode("A"))
// expect: QQ==

// padding 2 bytes
print(base64.encode("AB"))
// expect: QUI=

// URL-safe round-trip
var enc = base64.encode_url("Hello, World!")
var buf2 = base64.decode_url(enc)
print(buf2.to_str())
// expect: Hello, World!

// encode_raw (no padding)
print(base64.encode_raw("A"))
// expect: QQ

// is_valid
print(base64.is_valid("SGVs"))
// expect: true
print(base64.is_valid("bad$"))
// expect: false

// encoded_len
print(base64.encoded_len(3))
// expect: 4
print(base64.encoded_len(1))
// expect: 4
print(base64.encoded_len(0))
// expect: 0
