/* src/stdlib/bits.c - STDLIB-43: bits module
 *
 * Public API (all operate on int64_t / MS_INT_VAL):
 *   bits.ones_count(n)       → int   popcount
 *   bits.leading_zeros(n)    → int   leading zero count (64-bit)
 *   bits.trailing_zeros(n)   → int   trailing zero count
 *   bits.len(n)              → int   bit length (0 for n==0)
 *   bits.rotate_left(n, k)   → int   64-bit rotate left by k
 *   bits.rotate_right(n, k)  → int   64-bit rotate right by k
 *   bits.reverse(n)          → int   reverse all 64 bits
 *   bits.byte_swap(n)        → int   byte-swap (bswap64)
 *   bits.add(a, b)           → [int, int]  [result, carry]
 *   bits.mul(a, b)           → [int, int]  128-bit [hi, lo]
 *   bits.bit_and(a, b)       → int   (named to avoid clash with 'and' keyword)
 *   bits.bit_or(a, b)        → int   (named to avoid clash with 'or' keyword)
 *   bits.xor(a, b)           → int
 *   bits.bit_not(n)          → int   (named to avoid clash with 'not' keyword)
 *   bits.lshift(n, k)        → int   left shift
 *   bits.rshift(n, k)        → int   arithmetic right shift
 *   bits.urshift(n, k)       → int   logical (unsigned) right shift
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Platform bit intrinsics                                             */
/* ------------------------------------------------------------------ */

#ifdef _MSC_VER
#  include <intrin.h>
#  pragma intrinsic(_BitScanReverse64, _BitScanForward64, __popcnt64)

static int bits_popcount64(uint64_t x) {
    return (int)__popcnt64(x);
}
static int bits_clz64(uint64_t x) {
    if (x == 0) return 64;
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return 63 - (int)idx;
}
static int bits_ctz64(uint64_t x) {
    if (x == 0) return 64;
    unsigned long idx;
    _BitScanForward64(&idx, x);
    return (int)idx;
}
#else
static int bits_popcount64(uint64_t x) {
    return __builtin_popcountll((unsigned long long)x);
}
static int bits_clz64(uint64_t x) {
    if (x == 0) return 64;
    return __builtin_clzll((unsigned long long)x);
}
static int bits_ctz64(uint64_t x) {
    if (x == 0) return 64;
    return __builtin_ctzll((unsigned long long)x);
}
#endif

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Extract int64 from MsValue (int or number). Returns false on type error. */
static bool get_i64(MsVM* vm, MsValue v, uint64_t* out, const char* ctx) {
    if (MS_IS_INT(v)) { *out = (uint64_t)MS_AS_INT(v); return true; }
    if (MS_IS_NUMBER(v)) { *out = (uint64_t)(ms_i64)MS_AS_NUMBER(v); return true; }
    ms_vm_runtime_error(vm, "%s: expected int", ctx);
    return false;
}

/* Build a two-element List [a, b]. */
static MsValue make_pair(MsVM* vm, ms_i64 a, ms_i64 b) {
    MsValue items[2] = { MS_INT_VAL(a), MS_INT_VAL(b) };
    MsObjList* lst = ms_obj_list_from_array(vm, items, 2);
    return MS_OBJ_VAL((MsObject*)lst);
}

/* ------------------------------------------------------------------ */
/* Native functions                                                    */
/* ------------------------------------------------------------------ */

static MsValue bits_ones_count(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.ones_count")) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)bits_popcount64(n));
}

static MsValue bits_leading_zeros(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.leading_zeros")) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)bits_clz64(n));
}

static MsValue bits_trailing_zeros(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.trailing_zeros")) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)bits_ctz64(n));
}

static MsValue bits_len(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.len")) return MS_NIL_VAL();
    if (n == 0) return MS_INT_VAL(0);
    return MS_INT_VAL((ms_i64)(64 - bits_clz64(n)));
}

static MsValue bits_rotate_left(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.rotate_left")) return MS_NIL_VAL();
    uint64_t k;
    if (!get_i64(vm, argv[1], &k, "bits.rotate_left")) return MS_NIL_VAL();
    k &= 63;
    uint64_t r = (k == 0) ? n : ((n << k) | (n >> (64u - k)));
    return MS_INT_VAL((ms_i64)r);
}

static MsValue bits_rotate_right(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.rotate_right")) return MS_NIL_VAL();
    uint64_t k;
    if (!get_i64(vm, argv[1], &k, "bits.rotate_right")) return MS_NIL_VAL();
    k &= 63;
    uint64_t r = (k == 0) ? n : ((n >> k) | (n << (64u - k)));
    return MS_INT_VAL((ms_i64)r);
}

static MsValue bits_reverse(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.reverse")) return MS_NIL_VAL();
    /* Reverse 64 bits via standard bit-reversal technique */
    n = ((n & UINT64_C(0x5555555555555555)) << 1)  | ((n >> 1)  & UINT64_C(0x5555555555555555));
    n = ((n & UINT64_C(0x3333333333333333)) << 2)  | ((n >> 2)  & UINT64_C(0x3333333333333333));
    n = ((n & UINT64_C(0x0F0F0F0F0F0F0F0F)) << 4)  | ((n >> 4)  & UINT64_C(0x0F0F0F0F0F0F0F0F));
    n = ((n & UINT64_C(0x00FF00FF00FF00FF)) << 8)  | ((n >> 8)  & UINT64_C(0x00FF00FF00FF00FF));
    n = ((n & UINT64_C(0x0000FFFF0000FFFF)) << 16) | ((n >> 16) & UINT64_C(0x0000FFFF0000FFFF));
    n = (n << 32) | (n >> 32);
    return MS_INT_VAL((ms_i64)n);
}

static MsValue bits_byte_swap(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.byte_swap")) return MS_NIL_VAL();
    /* bswap64 portable */
    uint64_t r =
        ((n & UINT64_C(0x00000000000000FF)) << 56) |
        ((n & UINT64_C(0x000000000000FF00)) << 40) |
        ((n & UINT64_C(0x0000000000FF0000)) << 24) |
        ((n & UINT64_C(0x00000000FF000000)) << 8)  |
        ((n & UINT64_C(0x000000FF00000000)) >> 8)  |
        ((n & UINT64_C(0x0000FF0000000000)) >> 24) |
        ((n & UINT64_C(0x00FF000000000000)) >> 40) |
        ((n & UINT64_C(0xFF00000000000000)) >> 56);
    return MS_INT_VAL((ms_i64)r);
}

static MsValue bits_add(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t a, b;
    if (!get_i64(vm, argv[0], &a, "bits.add")) return MS_NIL_VAL();
    if (!get_i64(vm, argv[1], &b, "bits.add")) return MS_NIL_VAL();
    uint64_t result = a + b;
    ms_i64 carry = (result < a) ? 1 : 0;
    return make_pair(vm, (ms_i64)result, carry);
}

static MsValue bits_mul(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t a, b;
    if (!get_i64(vm, argv[0], &a, "bits.mul")) return MS_NIL_VAL();
    if (!get_i64(vm, argv[1], &b, "bits.mul")) return MS_NIL_VAL();
    /* 64x64->128 using 32-bit halves */
    uint64_t lo_a = a & UINT64_C(0xFFFFFFFF);
    uint64_t hi_a = a >> 32;
    uint64_t lo_b = b & UINT64_C(0xFFFFFFFF);
    uint64_t hi_b = b >> 32;
    uint64_t p0 = lo_a * lo_b;
    uint64_t p1 = lo_a * hi_b;
    uint64_t p2 = hi_a * lo_b;
    uint64_t p3 = hi_a * hi_b;
    uint64_t mid = (p0 >> 32) + (p1 & UINT64_C(0xFFFFFFFF)) + (p2 & UINT64_C(0xFFFFFFFF));
    uint64_t hi  = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    uint64_t lo  = (p0 & UINT64_C(0xFFFFFFFF)) | (mid << 32);
    return make_pair(vm, (ms_i64)hi, (ms_i64)lo);
}

static MsValue bits_bit_and(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t a, b;
    if (!get_i64(vm, argv[0], &a, "bits.bit_and")) return MS_NIL_VAL();
    if (!get_i64(vm, argv[1], &b, "bits.bit_and")) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)(a & b));
}

static MsValue bits_bit_or(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t a, b;
    if (!get_i64(vm, argv[0], &a, "bits.bit_or")) return MS_NIL_VAL();
    if (!get_i64(vm, argv[1], &b, "bits.bit_or")) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)(a | b));
}

static MsValue bits_xor(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t a, b;
    if (!get_i64(vm, argv[0], &a, "bits.xor")) return MS_NIL_VAL();
    if (!get_i64(vm, argv[1], &b, "bits.xor")) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)(a ^ b));
}

static MsValue bits_bit_not(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.bit_not")) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)(~n));
}

static MsValue bits_lshift(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n, k;
    if (!get_i64(vm, argv[0], &n, "bits.lshift")) return MS_NIL_VAL();
    if (!get_i64(vm, argv[1], &k, "bits.lshift")) return MS_NIL_VAL();
    uint64_t r = (k >= 64) ? 0 : (n << k);
    return MS_INT_VAL((ms_i64)r);
}

static MsValue bits_rshift(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n;
    if (!get_i64(vm, argv[0], &n, "bits.rshift")) return MS_NIL_VAL();
    uint64_t k;
    if (!get_i64(vm, argv[1], &k, "bits.rshift")) return MS_NIL_VAL();
    /* arithmetic (signed) right shift */
    ms_i64 sn = (ms_i64)n;
    ms_i64 r = (k >= 64) ? (sn < 0 ? -1 : 0) : (sn >> (int)k);
    return MS_INT_VAL(r);
}

static MsValue bits_urshift(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n, k;
    if (!get_i64(vm, argv[0], &n, "bits.urshift")) return MS_NIL_VAL();
    if (!get_i64(vm, argv[1], &k, "bits.urshift")) return MS_NIL_VAL();
    uint64_t r = (k >= 64) ? 0 : (n >> k);
    return MS_INT_VAL((ms_i64)r);
}

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef bits_defs[] = {
    {"ones_count",    bits_ones_count,    1},
    {"leading_zeros", bits_leading_zeros, 1},
    {"trailing_zeros",bits_trailing_zeros,1},
    {"len",           bits_len,           1},
    {"rotate_left",   bits_rotate_left,   2},
    {"rotate_right",  bits_rotate_right,  2},
    {"reverse",       bits_reverse,       1},
    {"byte_swap",     bits_byte_swap,     1},
    {"add",           bits_add,           2},
    {"mul",           bits_mul,           2},
    {"bit_and",       bits_bit_and,       2},
    {"bit_or",        bits_bit_or,        2},
    {"xor",           bits_xor,           2},
    {"bit_not",       bits_bit_not,       1},
    {"lshift",        bits_lshift,        2},
    {"rshift",        bits_rshift,        2},
    {"urshift",       bits_urshift,       2},
    {NULL, NULL, 0}
};

void ms_module_bits_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, bits_defs);
}
