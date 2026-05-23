import "hash"

// md5 streaming
var h1 = hash.new("md5")
hash.update(h1, "hel")
hash.update(h1, "lo")
assert(hash.hexdigest(h1) == "5d41402abc4b2a76b9719d911017c592")

// sha256 streaming: digest() returns bytes
var h2 = hash.new("sha256")
hash.update(h2, "hel")
hash.update(h2, "lo")
var d2 = hash.digest(h2)
assert(d2.len() == 32)
assert(d2.to_hex() == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824")

// sha1 streaming
var h3 = hash.new("sha1")
hash.update(h3, "hello")
assert(hash.hexdigest(h3) == "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d")

// reset and reuse
var h4 = hash.new("md5")
hash.update(h4, "hello")
var _ = hash.hexdigest(h4)
hash.reset(h4)
hash.update(h4, "hello")
assert(hash.hexdigest(h4) == "5d41402abc4b2a76b9719d911017c592")

print("ok")
// expect: ok
