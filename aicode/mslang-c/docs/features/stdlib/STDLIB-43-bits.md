# STDLIB-43: bits 模块

## 职责

整数位操作：位计数、前导零/尾部零、旋转、位长度。
对应 Go 的 `math/bits` 包。纯 C 实现，使用编译器 builtins（`__builtin_clzll` 等）。

---

## C/.ms 分层

全部 C（`src/stdlib/bits.c`）。操作 `MS_INT_VAL`（int64_t）。

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bits.ones_count(n)` | int | int | 置 1 位的数量（popcount）|
| `bits.leading_zeros(n)` | int | int | 高位连续 0 的数量（64 位）|
| `bits.trailing_zeros(n)` | int | int | 低位连续 0 的数量 |
| `bits.len(n)` | int | int | 最高置 1 位 + 1（0 的 len 为 0）|
| `bits.rotate_left(n, k)` | int, int | int | 循环左移 k 位（64 位）|
| `bits.rotate_right(n, k)` | int, int | int | 循环右移 k 位 |
| `bits.reverse(n)` | int | int | 翻转所有 64 位 |
| `bits.byte_swap(n)` | int | int | 字节顺序翻转（bswap64）|
| `bits.add(a, b)` | int, int | [int, int] | 无溢出加法，返回 [result, carry] |
| `bits.mul(a, b)` | int, int | [int, int] | 64×64→128，返回 [hi, lo] |

### 位运算（弥补语言层可能缺少的运算符）

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bits.and(a, b)` | int, int | int | 按位与 |
| `bits.or(a, b)` | int, int | int | 按位或 |
| `bits.xor(a, b)` | int, int | int | 按位异或 |
| `bits.not(n)` | int | int | 按位取反 |
| `bits.lshift(n, k)` | int, int | int | 左移 |
| `bits.rshift(n, k)` | int, int | int | 算术右移 |
| `bits.urshift(n, k)` | int, int | int | 逻辑右移（无符号）|

---

## 依赖

- CAPI-01/02
- 编译器 builtins（GCC/Clang：`__builtin_clzll`/`__builtin_ctzll`/`__builtin_popcountll`；MSVC：`_BitScanReverse64`/`__popcnt64`）

---

## 示例

```ms
import "bits"

print(bits.ones_count(0b10110101))  // 5
print(bits.leading_zeros(1))        // 63
print(bits.trailing_zeros(8))       // 3
print(bits.len(255))                // 8
print(bits.rotate_left(1, 3))       // 8
print(bits.byte_swap(0x0102030405060708))  // 0x0807060504030201
```

---

## 测试

```
tests/unit/test_stdlib_bits.c
tests/fixtures/stdlib_bits_basic.ms
```
