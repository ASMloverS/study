import "hash"
import "buffer"

// 一次性哈希（返回 Buffer）
var md5_buf = hash.md5("hello")
print(md5_buf.len())           // 16
print(md5_buf.to_hex())        // 5d41402abc4b2a76b9719d911017c592

var sha1_buf = hash.sha1("hello")
print(sha1_buf.len())          // 20
print(sha1_buf.to_hex())       // aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d

var sha256_buf = hash.sha256("hello")
print(sha256_buf.len())        // 32
print(sha256_buf.to_hex())     // 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824

// CRC32 和 FNV1a（返回整数）
print(hash.crc32("hello"))     // 907060870
print(hash.fnv1a("hello", 32)) // 1335831723
print(hash.fnv1a("hello", 64)) // -6615550055289275125（i64）

// 也可以接受 Buffer
var b = buffer.from_str("hello")
print(hash.crc32(b))           // 907060870（与字符串结果一致）

// 流式 Hasher：分块喂数据
var h = hash.new("sha256")
hash.update(h, "hel")
hash.update(h, "lo")
print(hash.hexdigest(h))       // 2cf24dba...（与一次性结果一致）

// 复用 Hasher（reset 后重新计算）
hash.reset(h)
hash.update(h, "world")
print(hash.hexdigest(h))       // 486ea46224d1bb4fb680f34f7c9ad96a8f24ec88be73ea8e5a6c65260e9cb8a7
