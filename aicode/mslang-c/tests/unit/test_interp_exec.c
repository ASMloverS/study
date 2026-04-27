#include "test_assert.h"
#include "ms/vm.h"
#include <stdio.h>

static MsInterpretResult run(const char* src) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, src, "<test>");
    ms_vm_free(&vm);
    return r;
}

static void test_simple_interp(void) {
    MsInterpretResult r = run("var name = \"world\"\nprint(\"hello ${name}!\")");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
}

static void test_expr_interp(void) {
    MsInterpretResult r = run("print(\"${1 + 2} items\")");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
}

static void test_number_interp(void) {
    TEST_ASSERT_EQ(run("var x = 42\nprint(\"x = ${x}\")"), MS_INTERPRET_OK);
}

static void test_bool_interp(void) {
    TEST_ASSERT_EQ(run("print(\"${true} or ${false}\")"), MS_INTERPRET_OK);
}

static void test_nil_interp(void) {
    TEST_ASSERT_EQ(run("var n = nil\nprint(\"value is ${n}\")"), MS_INTERPRET_OK);
}

static void test_multi_segment_interp(void) {
    TEST_ASSERT_EQ(run("var a = \"A\"\nvar b = \"B\"\nprint(\"${a} and ${b}\")"), MS_INTERPRET_OK);
}

static void test_pure_number_interp(void) {
    TEST_ASSERT_EQ(run("print(\"${100}\")"), MS_INTERPRET_OK);
}

int main(void) {
    test_simple_interp();
    test_expr_interp();
    test_number_interp();
    test_bool_interp();
    test_nil_interp();
    test_multi_segment_interp();
    test_pure_number_interp();
    printf("test_interp_exec: all passed\n");
    return 0;
}
