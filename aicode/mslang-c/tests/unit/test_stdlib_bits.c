/* tests/unit/test_stdlib_bits.c - STDLIB-43: bits module */
#include "ms/vm.h"
#include <stdio.h>
#include <stdlib.h>

static MsInterpretResult run_snippet(const char* src) {
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) { fprintf(stderr, "OOM\n"); exit(1); }
    ms_vm_init(vm);
    MsInterpretResult r = ms_vm_interpret(vm, src, "<test>");
    ms_vm_free(vm);
    free(vm);
    return r;
}

static void check_ok(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_OK) {
        fprintf(stderr, "FAIL test_stdlib_bits.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

static void test_ones_count(void) {
    RUN_OK("import \"bits\"\nassert(bits.ones_count(0) == 0)");
    RUN_OK("import \"bits\"\nassert(bits.ones_count(1) == 1)");
    RUN_OK("import \"bits\"\nassert(bits.ones_count(0b10110101) == 5)");
    RUN_OK("import \"bits\"\nassert(bits.ones_count(-1) == 64)");
}

static void test_leading_zeros(void) {
    RUN_OK("import \"bits\"\nassert(bits.leading_zeros(0) == 64)");
    RUN_OK("import \"bits\"\nassert(bits.leading_zeros(1) == 63)");
    RUN_OK("import \"bits\"\nassert(bits.leading_zeros(256) == 55)");
}

static void test_trailing_zeros(void) {
    RUN_OK("import \"bits\"\nassert(bits.trailing_zeros(0) == 64)");
    RUN_OK("import \"bits\"\nassert(bits.trailing_zeros(1) == 0)");
    RUN_OK("import \"bits\"\nassert(bits.trailing_zeros(8) == 3)");
    RUN_OK("import \"bits\"\nassert(bits.trailing_zeros(16) == 4)");
}

static void test_len(void) {
    RUN_OK("import \"bits\"\nassert(bits.len(0) == 0)");
    RUN_OK("import \"bits\"\nassert(bits.len(1) == 1)");
    RUN_OK("import \"bits\"\nassert(bits.len(255) == 8)");
    RUN_OK("import \"bits\"\nassert(bits.len(256) == 9)");
}

static void test_rotate(void) {
    RUN_OK("import \"bits\"\nassert(bits.rotate_left(1, 3) == 8)");
    RUN_OK("import \"bits\"\nassert(bits.rotate_left(8, 0) == 8)");
    RUN_OK("import \"bits\"\nassert(bits.rotate_right(8, 3) == 1)");
    /* rotate by 64 is identity */
    RUN_OK("import \"bits\"\nassert(bits.rotate_left(42, 64) == 42)");
    RUN_OK("import \"bits\"\nassert(bits.rotate_right(42, 64) == 42)");
}

static void test_reverse(void) {
    /* reverse of 1 (LSB) → 0x8000000000000000 (MSB set) */
    RUN_OK(
        "import \"bits\"\n"
        "var r = bits.reverse(1)\n"
        "assert(bits.leading_zeros(r) == 0)\n"
        "assert(bits.trailing_zeros(r) == 63)\n"
    );
    RUN_OK("import \"bits\"\nassert(bits.reverse(0) == 0)");
    /* reverse is its own inverse */
    RUN_OK("import \"bits\"\nassert(bits.reverse(bits.reverse(12345)) == 12345)");
}

static void test_byte_swap(void) {
    /* 0x0102030405060708 swapped → 0x0807060504030201 */
    RUN_OK(
        "import \"bits\"\n"
        "var v = 0x0102030405060708\n"
        "var s = bits.byte_swap(v)\n"
        "assert(bits.byte_swap(s) == v)\n"
    );
    RUN_OK("import \"bits\"\nassert(bits.byte_swap(0) == 0)");
}

static void test_add(void) {
    RUN_OK(
        "import \"bits\"\n"
        "var r = bits.add(3, 5)\n"
        "assert(r[0] == 8)\n"
        "assert(r[1] == 0)\n"
    );
    /* overflow: UINT64_MAX + 1 = [0, carry=1] */
    RUN_OK(
        "import \"bits\"\n"
        "var r = bits.add(-1, 1)\n"
        "assert(r[0] == 0)\n"
        "assert(r[1] == 1)\n"
    );
}

static void test_mul(void) {
    RUN_OK(
        "import \"bits\"\n"
        "var r = bits.mul(3, 4)\n"
        "assert(r[0] == 0)\n"
        "assert(r[1] == 12)\n"
    );
    /* 0 * anything */
    RUN_OK(
        "import \"bits\"\n"
        "var r = bits.mul(0, 999)\n"
        "assert(r[0] == 0)\n"
        "assert(r[1] == 0)\n"
    );
}

static void test_bitwise_ops(void) {
    RUN_OK("import \"bits\"\nassert(bits.bit_and(0b1010, 0b1100) == 0b1000)");
    RUN_OK("import \"bits\"\nassert(bits.bit_or(0b1010, 0b0101) == 0b1111)");
    RUN_OK("import \"bits\"\nassert(bits.xor(0b1010, 0b1100) == 0b0110)");
    RUN_OK("import \"bits\"\nassert(bits.bit_not(0) == -1)");
}

static void test_shifts(void) {
    RUN_OK("import \"bits\"\nassert(bits.lshift(1, 4) == 16)");
    RUN_OK("import \"bits\"\nassert(bits.lshift(1, 63) != 0)");
    RUN_OK("import \"bits\"\nassert(bits.lshift(1, 64) == 0)");
    RUN_OK("import \"bits\"\nassert(bits.rshift(16, 4) == 1)");
    /* arithmetic right shift preserves sign */
    RUN_OK("import \"bits\"\nassert(bits.rshift(-8, 2) == -2)");
    /* logical right shift fills with zeros */
    RUN_OK("import \"bits\"\nassert(bits.urshift(-1, 63) == 1)");
    RUN_OK("import \"bits\"\nassert(bits.urshift(16, 4) == 1)");
}

int main(void) {
    test_ones_count();
    test_leading_zeros();
    test_trailing_zeros();
    test_len();
    test_rotate();
    test_reverse();
    test_byte_swap();
    test_add();
    test_mul();
    test_bitwise_ops();
    test_shifts();
    printf("test_stdlib_bits: all passed\n");
    return 0;
}
