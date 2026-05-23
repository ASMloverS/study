# hash 模块

MD5 / SHA1 / SHA256 / CRC32 / FNV1a 哈希，无外部依赖。支持一次性函数和流式 Hasher 两种用法。

```ms
import "hash"
```

> 实现规格：[STDLIB-06-hash.md](../STDLIB-06-hash.md)

---

## 函数速查表

### 一次性函数

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `hash.md5(data)` | str \| Buffer | Buffer（16 B） | MD5 摘要 |
| `hash.sha1(data)` | str \| Buffer | Buffer（20 B） | SHA-1 摘要 |
| `hash.sha256(data)` | str \| Buffer | Buffer（32 B） | SHA-256 摘要 |
| `hash.crc32(data)` | str \| Buffer | int | CRC-32 校验值 |
| `hash.fnv1a(data[, bits])` | str \| Buffer, 32\|64 | int | FNV-1a，bits 默认 64 |

### 流式 Hasher

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `hash.new(algo)` | str | Hasher | 创建 Hasher，algo："md5" / "sha1" / "sha256" |
| `hash.update(h, data)` | Hasher, str \| Buffer | nil | 追加数据到 Hasher |
| `hash.digest(h)` | Hasher | Buffer | 完成计算，返回摘要字节；**调用后 Hasher 已终结** |
| `hash.hexdigest(h)` | Hasher | str | 完成计算，返回十六进制字符串；**同上** |
| `hash.reset(h)` | Hasher | nil | 重置 Hasher，可重新使用 |

---

## 分组详解

### 一次性哈希

最简单的用法：直接传入字符串或 Buffer。

```ms
import "hash"
import "buffer"

// 返回原始字节 Buffer
var d = hash.md5("hello")
print(d.to_hex())   // 5d41402abc4b2a76b9719d911017c592

// SHA-256
print(hash.sha256("hello").to_hex())
// 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824

// 传入 Buffer 与传入字符串结果相同
var b = buffer.from_str("hello")
print(hash.crc32(b) == hash.crc32("hello"))   // true
```

### CRC32 与 FNV1a

```ms
import "hash"
print(hash.crc32("hello"))         // 907060870（int）
print(hash.fnv1a("hello", 32))     // 1335831723
print(hash.fnv1a("hello", 64))     // -6615550055289275125（i64，有符号）
print(hash.fnv1a("hello"))         // 默认 64 bit，同上
```

### 流式 Hasher（分块输入）

适合大文件或多次 `update` 的场景：

```ms
import "hash"

var h = hash.new("sha256")
hash.update(h, "hel")
hash.update(h, "lo")
print(hash.hexdigest(h))
// 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824

// 复用同一个 Hasher
hash.reset(h)
hash.update(h, "world")
print(hash.hexdigest(h))
// 486ea46224d1bb4fb680f34f7c9ad96a8f24ec88be73ea8e5a6c65260e9cb8a7
```

---

## 完整示例

文件：[`examples/hash.ms`](examples/hash.ms)

```ms
import "hash"
import "buffer"

var md5_buf = hash.md5("hello")
print(md5_buf.len())
print(md5_buf.to_hex())

var sha1_buf = hash.sha1("hello")
print(sha1_buf.len())
print(sha1_buf.to_hex())

var sha256_buf = hash.sha256("hello")
print(sha256_buf.len())
print(sha256_buf.to_hex())

print(hash.crc32("hello"))
print(hash.fnv1a("hello", 32))
print(hash.fnv1a("hello", 64))

var b = buffer.from_str("hello")
print(hash.crc32(b))

var h = hash.new("sha256")
hash.update(h, "hel")
hash.update(h, "lo")
print(hash.hexdigest(h))

hash.reset(h)
hash.update(h, "world")
print(hash.hexdigest(h))
```

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/hash.ms
16
5d41402abc4b2a76b9719d911017c592
20
aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d
32
2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
907060870
1335831723
-6615550055289275125
907060870
2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
486ea46224d1bb4fb680f34f7c9ad96a8f24ec88be73ea8e5a6c65260e9cb8a7
```

---

## 实现/性能注解

- **MD5 / SHA1 / SHA256** 均为纯 C 内嵌实现，无外部依赖（`src/stdlib/hash_impl.c`）。
- MD5 和 SHA1 已不适合用于安全场景（碰撞攻击），仅适合文件校验或非安全标识。
- `fnv1a` 64-bit 返回 `i64`（有符号），如需无符号解释请在脚本里自行转换。
- `digest` / `hexdigest` 会**终结** Hasher，之后调用 `update` 或再次 `digest` 会报错，需先 `reset`。

## 常见陷阱

```ms
import "hash"
var h = hash.new("sha256")
hash.update(h, "foo")
var r1 = hash.hexdigest(h)   // 正常

// ❌ digest 已终结，再次调用报运行时错误
// hash.update(h, "bar")  → 错误

// ✅ reset 后才能重用
hash.reset(h)
hash.update(h, "bar")
var r2 = hash.hexdigest(h)
```
