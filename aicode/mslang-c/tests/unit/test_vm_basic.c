#include "test_assert.h"
#include "ms/vm.h"
#include <stdio.h>

/* Helper: interpret source, expect OK */
static MsInterpretResult run(const char* src) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, src, "<test>");
    ms_vm_free(&vm);
    return r;
}

static void test_simple_arithmetic(void) {
    MsInterpretResult r;
    r = run("print(1 + 2)");   TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    r = run("print(10 - 4)");  TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    r = run("print(3 * 4)");   TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    r = run("print(10 / 4)");  TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    r = run("print(10 % 3)");  TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    r = run("print(-7)");      TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
}

static void test_globals(void) {
    TEST_ASSERT_EQ(run("var x = 10\nvar y = 20\nprint(x + y)"), MS_INTERPRET_OK);
}

static void test_if_else_runtime(void) {
    TEST_ASSERT_EQ(run("if (1 < 2) { print(1) } else { print(0) }"), MS_INTERPRET_OK);
    TEST_ASSERT_EQ(run("if (2 < 1) { print(0) } else { print(1) }"), MS_INTERPRET_OK);
}

static void test_while_runtime(void) {
    TEST_ASSERT_EQ(run("var i = 0\nwhile (i < 3) { print(i)\ni = i + 1\n}"), MS_INTERPRET_OK);
}

static void test_function_call(void) {
    TEST_ASSERT_EQ(run("fun sq(x) { return x * x }\nprint(sq(5))"), MS_INTERPRET_OK);
}

static void test_string_concat(void) {
    TEST_ASSERT_EQ(run("print(\"hello\" + \" \" + \"world\")"), MS_INTERPRET_OK);
}

static void test_comparison(void) {
    TEST_ASSERT_EQ(run("print(1 < 2)\nprint(2 > 1)\nprint(1 == 1)\nprint(1 != 2)"), MS_INTERPRET_OK);
}

static void test_logic(void) {
    TEST_ASSERT_EQ(run("print(!false)\nprint(!true)"), MS_INTERPRET_OK);
}

static void test_undefined_var_error(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, "print(z)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_RUNTIME_ERROR);
    ms_vm_free(&vm);
}

static void test_compile_error(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, "fun (", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_COMPILE_ERROR);
    ms_vm_free(&vm);
}

int main(void) {
    test_simple_arithmetic();
    test_globals();
    test_if_else_runtime();
    test_while_runtime();
    test_function_call();
    test_string_concat();
    test_comparison();
    test_logic();
    test_undefined_var_error();
    test_compile_error();
    printf("test_vm_basic: all passed\n");
    return 0;
}
