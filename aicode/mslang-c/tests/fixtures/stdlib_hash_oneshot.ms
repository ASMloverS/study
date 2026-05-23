import "hash"

// md5
var d_md5 = hash.md5("hello")
assert(d_md5.len() == 16)
assert(d_md5.to_hex() == "5d41402abc4b2a76b9719d911017c592")

var d_md5_empty = hash.md5("")
assert(d_md5_empty.to_hex() == "d41d8cd98f00b204e9800998ecf8427e")

// sha1
var d_sha1 = hash.sha1("hello")
assert(d_sha1.len() == 20)
assert(d_sha1.to_hex() == "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d")

// sha256
var d_sha256 = hash.sha256("hello")
assert(d_sha256.len() == 32)
assert(d_sha256.to_hex() == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824")

var d_sha256_empty = hash.sha256("")
assert(d_sha256_empty.to_hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")

// sha256 accepts buffer input
import "buffer"
var buf = buffer.from_str("hello")
var d_buf = hash.sha256(buf)
assert(d_buf.to_hex() == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824")

// crc32
assert(hash.crc32("hello") == 0x3610a686)
assert(hash.crc32("") == 0)

// fnv1a (32-bit)
assert(hash.fnv1a("hello", 32) == 0x4f9f2cab)

// fnv1a (64-bit default, signed i64)
assert(hash.fnv1a("hello") == -6615550055289275125)

print("ok")
// expect: ok
