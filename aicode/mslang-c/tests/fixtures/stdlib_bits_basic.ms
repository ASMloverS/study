import "bits"

// ones_count
print(bits.ones_count(0b10110101))
// expect: 5
print(bits.ones_count(0))
// expect: 0
print(bits.ones_count(-1))
// expect: 64

// leading_zeros
print(bits.leading_zeros(1))
// expect: 63
print(bits.leading_zeros(0))
// expect: 64

// trailing_zeros
print(bits.trailing_zeros(8))
// expect: 3
print(bits.trailing_zeros(0))
// expect: 64

// len
print(bits.len(255))
// expect: 8
print(bits.len(0))
// expect: 0

// rotate_left
print(bits.rotate_left(1, 3))
// expect: 8

// rotate_right
print(bits.rotate_right(8, 3))
// expect: 1

// reverse: reverse(1) has MSB set, trailing_zeros=63
print(bits.trailing_zeros(bits.reverse(1)))
// expect: 63

// byte_swap roundtrip
print(bits.byte_swap(bits.byte_swap(0x0102030405060708)) == 0x0102030405060708)
// expect: true

// add
var add_r = bits.add(3, 5)
print(add_r[0])
// expect: 8
print(add_r[1])
// expect: 0

// mul
var mul_r = bits.mul(6, 7)
print(mul_r[1])
// expect: 42

// bitwise ops (bit_and/bit_or/bit_not avoid clash with language keywords)
print(bits.bit_and(0b1010, 0b1100))
// expect: 8
print(bits.bit_or(0b0101, 0b1010))
// expect: 15
print(bits.xor(0b1111, 0b1010))
// expect: 5
print(bits.bit_not(0) == -1)
// expect: true

// shifts
print(bits.lshift(1, 4))
// expect: 16
print(bits.rshift(16, 4))
// expect: 1
print(bits.rshift(-8, 2))
// expect: -2
print(bits.urshift(-1, 63))
// expect: 1

print("bits_ok")
// expect: bits_ok
