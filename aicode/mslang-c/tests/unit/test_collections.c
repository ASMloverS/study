/* tests/unit/test_collections.c - T21: List, Map, Tuple */
#include "../test_assert.h"
#include "ms/vm.h"
#include <stdio.h>

static void test_list_basic(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var a = [1, 2, 3]\nprint(a[1])", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_list_set(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var a = [10, 20, 30]\na[1] = 99\nprint(a[1])", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_list_negative_index(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var a = [10, 20, 30]\nprint(a[-1])", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_list_empty(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var b = []\nprint(b)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_map_basic(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var m = {\"name\": \"Alice\"}\nprint(m[\"name\"])", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_map_set(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var m = {\"a\": 1}\nm[\"b\"] = 2\nprint(m[\"b\"])", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_tuple_basic(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var t = (1, \"hello\", true)\nprint(t[0])\nprint(t[1])\nprint(t[2])", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_nested_list(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var matrix = [[1, 2], [3, 4]]\nprint(matrix[0][0])\nprint(matrix[1][1])", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_list_basic();
    test_list_set();
    test_list_negative_index();
    test_list_empty();
    test_map_basic();
    test_map_set();
    test_tuple_basic();
    test_nested_list();
    printf("test_collections: all passed\n");
    return 0;
}
