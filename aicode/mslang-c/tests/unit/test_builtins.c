/* tests/unit/test_builtins.c - T22: Built-in type methods */
#include "../test_assert.h"
#include "ms/vm.h"
#include <stdio.h>

#define RUN(src) do { \
    MsVM vm; ms_vm_init(&vm); \
    MsInterpretResult _r = ms_vm_interpret(&vm, (src), "<test>"); \
    TEST_ASSERT_EQ(_r, MS_INTERPRET_OK); \
    ms_vm_free(&vm); \
} while (0)

static void test_string_len(void) {
    RUN("print(\"hello\".len())");
}

static void test_string_upper_lower(void) {
    RUN("print(\"Hello\".upper())\nprint(\"Hello\".lower())");
}

static void test_string_contains(void) {
    RUN("print(\"hello world\".contains(\"world\"))");
    RUN("print(\"hello\".contains(\"xyz\"))");
}

static void test_string_starts_ends_with(void) {
    RUN("print(\"hello\".starts_with(\"hel\"))");
    RUN("print(\"hello\".ends_with(\"llo\"))");
}

static void test_string_index_of(void) {
    RUN("print(\"hello\".index_of(\"ll\"))");
    RUN("print(\"hello\".index_of(\"xyz\"))");
}

static void test_string_trim(void) {
    RUN("print(\"  hello  \".trim())");
}

static void test_string_replace(void) {
    RUN("print(\"hello\".replace(\"ll\", \"r\"))");
}

static void test_string_slice(void) {
    RUN("print(\"hello\".slice(1, 3))");
}

static void test_string_split(void) {
    RUN("print(\"a,b,c\".split(\",\").len())");
}

static void test_list_len(void) {
    RUN("var a = [1, 2, 3]\nprint(a.len())");
}

static void test_list_push_pop(void) {
    RUN("var a = [1, 2]\na.push(3)\nprint(a.len())\nprint(a.pop())");
}

static void test_list_contains_index_of(void) {
    RUN("var a = [10, 20, 30]\nprint(a.contains(20))\nprint(a.index_of(30))");
}

static void test_list_remove(void) {
    RUN("var a = [1, 2, 3]\na.remove(1)\nprint(a.len())");
}

static void test_list_sort_reverse(void) {
    RUN("var a = [3, 1, 2]\na.sort()\nprint(a[0])\na.reverse()\nprint(a[0])");
}

static void test_list_slice(void) {
    RUN("var a = [1, 2, 3, 4]\nprint(a.slice(1, 3).len())");
}

static void test_list_map(void) {
    RUN("var r = [1,2,3].map(fun(x) { return x * 2 })\nprint(r[0])\nprint(r[2])");
}

static void test_list_filter(void) {
    RUN("var r = [1,2,3,4,5].filter(fun(x) { return x % 2 == 0 })\nprint(r.len())");
}

static void test_list_join(void) {
    RUN("print([1,2,3].join(\"-\"))");
}

static void test_map_len(void) {
    RUN("var m = {\"a\": 1, \"b\": 2}\nprint(m.len())");
}

static void test_map_has(void) {
    RUN("var m = {\"a\": 1}\nprint(m.has(\"a\"))\nprint(m.has(\"z\"))");
}

static void test_map_remove(void) {
    RUN("var m = {\"a\": 1, \"b\": 2}\nm.remove(\"a\")\nprint(m.len())");
}

static void test_map_keys_values(void) {
    RUN("var m = {\"x\": 10}\nprint(m.keys().len())\nprint(m.values().len())");
}

static void test_tuple_len(void) {
    RUN("var t = (1, 2, 3)\nprint(t.len())");
}

static void test_tuple_contains(void) {
    RUN("var t = (1, 2, 3)\nprint(t.contains(2))\nprint(t.contains(9))");
}

int main(void) {
    test_string_len();
    test_string_upper_lower();
    test_string_contains();
    test_string_starts_ends_with();
    test_string_index_of();
    test_string_trim();
    test_string_replace();
    test_string_slice();
    test_string_split();
    test_list_len();
    test_list_push_pop();
    test_list_contains_index_of();
    test_list_remove();
    test_list_sort_reverse();
    test_list_slice();
    test_list_map();
    test_list_filter();
    test_list_join();
    test_map_len();
    test_map_has();
    test_map_remove();
    test_map_keys_values();
    test_tuple_len();
    test_tuple_contains();
    printf("test_builtins: all passed\n");
    return 0;
}
