import "buffer"

// 创建缓冲区
var b = buffer.new(4, 0)      // 4 字节，初始值 0
print(b.len())                // 4
print(b.get(0))               // 0

// 从字符串构造
var b2 = buffer.from_str("Hello")
print(b2.len())               // 5
print(b2.to_str())            // Hello
print(b2.to_hex())            // 48656c6c6f

// 从十六进制字符串构造
var b3 = buffer.from_hex("deadbeef")
print(b3.len())               // 4
print(b3.get(0))              // 222  (0xDE)

// append 追加
var b4 = buffer.from_str("Foo")
b4.append(" Bar")
print(b4.to_str())            // Foo Bar

// slice 切片
var b5 = buffer.from_str("Hello, World")
var s = b5.slice(7, 12)
print(s.to_str())             // World

// find 查找
print(b5.find(buffer.from_str("World")))   // 7
print(b5.find(buffer.from_str("xyz")))     // -1

// concat（静态形式）
var a = buffer.from_str("AB")
var bb = buffer.from_str("CD")
var c = buffer.concat(a, bb)
print(c.to_str())             // ABCD

// equals
print(a.equals(buffer.from_str("AB")))    // true
print(a.equals(buffer.from_str("XY")))   // false

// to_hex 与 from_hex 往返
var orig = buffer.from_str("mslang")
var hex = orig.to_hex()
print(hex)                               // 6d736c616e67
var back = buffer.from_hex(hex)
print(back.to_str())                     // mslang
