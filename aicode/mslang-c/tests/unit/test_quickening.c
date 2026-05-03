#include "test_assert.h"
#include "ms/vm.h"
#include <stdio.h>

static void test_add_quickens_to_ii(void) {
    MsVM vm; ms_vm_init(&vm);
    /* Loop integer addition triggers quickening to ADD_II */
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var sum = 0\n"
        "for (var i = 0; i < 10; i = i + 1) { sum = sum + i }\n"
        "print(sum)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_deopt(void) {
    MsVM vm; ms_vm_init(&vm);
    /* Integer add (quicken to II), then float (deopt + requicken to FF),
       then string (deopt again) */
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun add(a, b) { return a + b }\n"
        "print(add(1, 2))\n"
        "print(add(1.5, 2.5))\n"
        "print(add(\"a\", \"b\"))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_sub_quicken(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun sub(a, b) { return a - b }\n"
        "print(sub(10, 3))\n"
        "print(sub(10, 3))\n"
        "print(sub(10, 3))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_mul_quicken(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun mul(a, b) { return a * b }\n"
        "print(mul(3, 4))\n"
        "print(mul(3, 4))\n"
        "print(mul(3, 4))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_div_quicken(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun div(a, b) { return a / b }\n"
        "print(div(10.0, 2.0))\n"
        "print(div(10.0, 2.0))\n"
        "print(div(10.0, 2.0))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_lt_quicken(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun less(a, b) { return a < b }\n"
        "print(less(1, 2))\n"
        "print(less(1, 2))\n"
        "print(less(1, 2))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_lt_deopt_to_ff(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun less(a, b) { return a < b }\n"
        "print(less(1, 2))\n"
        "print(less(2.0, 1.0))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_eq_quicken(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun eq(a, b) { return a == b }\n"
        "print(eq(1, 1))\n"
        "print(eq(1, 2))\n"
        "print(eq(1, 1))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_float_loop(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var x = 0.0\n"
        "for (var i = 0; i < 5; i = i + 1) { x = x + 1.5 }\n"
        "print(x > 0.0)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_string_loop(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var s = \"\"\n"
        "for (var i = 0; i < 3; i = i + 1) { s = s + \"x\" }\n"
        "print(s)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_add_quickens_to_ii();
    test_deopt();
    test_sub_quicken();
    test_mul_quicken();
    test_div_quicken();
    test_lt_quicken();
    test_lt_deopt_to_ff();
    test_eq_quicken();
    test_float_loop();
    test_string_loop();
    printf("test_quickening: all passed\n");
    return 0;
}
