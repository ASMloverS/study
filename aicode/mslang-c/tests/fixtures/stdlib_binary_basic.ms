import "binary"

// calc_size
print(binary.calc_size(">IH"))
// expect: 6
print(binary.calc_size("<2H"))
// expect: 4
print(binary.calc_size(">4s"))
// expect: 4

// pack big-endian
var b = binary.pack(">IH", 0xDEADBEEF, 0x1234)
print(b.len())
// expect: 6
print(b.get(0))
// expect: 222
print(b.get(4))
// expect: 18

// unpack big-endian
var vals = binary.unpack(">IH", b)
print(vals[0])
// expect: 3735928559
print(vals[1])
// expect: 4660

// pack little-endian
var bl = binary.pack("<H", 0x0102)
print(bl.get(0))
// expect: 2
print(bl.get(1))
// expect: 1

// unpack with offset
var boff = binary.pack(">BH", 0xFF, 0xABCD)
var off_vals = binary.unpack(">H", boff, 1)
print(off_vals[0])
// expect: 43981

// varint encode/decode
var vb = binary.varint_encode(300)
print(vb.len())
// expect: 2
print(vb.get(0))
// expect: 172
print(vb.get(1))
// expect: 2

var vr = binary.varint_decode(vb)
print(vr[0])
// expect: 300
print(vr[1])
// expect: 2

// svarint encode/decode
var sb = binary.svarint_encode(-1)
var sr = binary.svarint_decode(sb)
print(sr[0])
// expect: -1

var sb2 = binary.svarint_encode(100)
var sr2 = binary.svarint_decode(sb2)
print(sr2[0])
// expect: 100

// read_u8
var rb = binary.pack(">B", 0xAB)
print(binary.read_u8(rb, 0))
// expect: 171

// read_u16_be
var r16 = binary.pack(">H", 0x1234)
print(binary.read_u16_be(r16, 0))
// expect: 4660

// read_u16_le
var r16l = binary.pack("<H", 0x1234)
print(binary.read_u16_le(r16l, 0))
// expect: 4660

// read_u32_be
var r32 = binary.pack(">I", 0x00010203)
print(binary.read_u32_be(r32, 0))
// expect: 66051

// read_u32_le
var r32l = binary.pack("<I", 0x00010203)
print(binary.read_u32_le(r32l, 0))
// expect: 66051
