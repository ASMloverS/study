/* tests/unit/test_stdlib_buffer.c - STDLIB-05: buffer module */
#include "../test_assert.h"
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

#define RUN_OK(src)  TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK)
#define RUN_ERR(src) TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_RUNTIME_ERROR)

/* ---- buffer.new ---- */

static void test_new_empty(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.new()\n"
        "assert(b.len() == 0)\n"
        "assert(b.cap() == 0)"
    );
}

static void test_new_with_size_and_fill(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.new(4, 0xff)\n"
        "assert(b.len() == 4)\n"
        "assert(b.get(0) == 255)\n"
        "assert(b.get(3) == 255)"
    );
}

/* ---- buffer.from_str ---- */

static void test_from_str(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello\")\n"
        "assert(b.len() == 5)\n"
        "assert(b.get(0) == 104)\n"   /* 'h' */
        "assert(b.get(4) == 111)"     /* 'o' */
    );
}

/* ---- buffer.from_hex ---- */

static void test_from_hex_basic(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_hex(\"deadbeef\")\n"
        "assert(b.len() == 4)\n"
        "assert(b.get(0) == 0xde)\n"
        "assert(b.get(1) == 0xad)\n"
        "assert(b.get(2) == 0xbe)\n"
        "assert(b.get(3) == 0xef)"
    );
}

static void test_from_hex_odd_length_error(void) {
    /* 1: from_hex odd length should trigger runtime error */
    RUN_ERR(
        "import \"buffer\"\n"
        "var b = buffer.from_hex(\"abc\")"
    );
}

static void test_from_hex_uppercase(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_hex(\"DEADBEEF\")\n"
        "assert(b.len() == 4)\n"
        "assert(b.get(0) == 0xde)"
    );
}

/* ---- buffer.concat (static) ---- */

static void test_concat_static(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var a = buffer.from_str(\"foo\")\n"
        "var b = buffer.from_str(\"bar\")\n"
        "var c = buffer.concat(a, b)\n"
        "assert(c.len() == 6)\n"
        "assert(c.to_str() == \"foobar\")"
    );
}

/* ---- get / set with negative index ---- */

static void test_get_negative_index(void) {
    /* 2: get/set negative index: -1 = last byte */
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"abc\")\n"
        "assert(b.get(-1) == 99)\n"   /* 'c' */
        "assert(b.get(-2) == 98)\n"   /* 'b' */
        "assert(b.get(-3) == 97)"     /* 'a' */
    );
}

static void test_set_negative_index(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"abc\")\n"
        "b.set(-1, 88)\n"             /* 'X' */
        "assert(b.get(2) == 88)"
    );
}

static void test_get_out_of_bounds_returns_nil(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"ab\")\n"
        "assert(b.get(5) == nil)\n"
        "assert(b.get(-5) == nil)"
    );
}

/* ---- slice ---- */

static void test_slice(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello world\")\n"
        "var c = b.slice(0, 5)\n"
        "assert(c.to_str() == \"hello\")"
    );
}

static void test_slice_end_minus1(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello\")\n"
        "var c = b.slice(2, -1)\n"
        "assert(c.to_str() == \"llo\")"
    );
}

/* ---- append ---- */

static void test_append_str(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello\")\n"
        "b.append(\" world\")\n"
        "assert(b.len() == 11)\n"
        "assert(b.to_str() == \"hello world\")"
    );
}

static void test_append_buffer(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var a = buffer.from_str(\"foo\")\n"
        "var b = buffer.from_str(\"bar\")\n"
        "a.append(b)\n"
        "assert(a.to_str() == \"foobar\")"
    );
}

/* ---- prepend ---- */

static void test_prepend(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"world\")\n"
        "b.prepend(\"hello \")\n"
        "assert(b.to_str() == \"hello world\")"
    );
}

/* ---- fill ---- */

static void test_fill(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.new(4, 0)\n"
        "b.fill(0xAA, 1, 3)\n"
        "assert(b.get(0) == 0)\n"
        "assert(b.get(1) == 0xAA)\n"
        "assert(b.get(2) == 0xAA)\n"
        "assert(b.get(3) == 0)"
    );
}

/* ---- clear ---- */

static void test_clear(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello\")\n"
        "b.clear()\n"
        "assert(b.len() == 0)"
    );
}

/* ---- resize ---- */

static void test_resize_grow_fill(void) {
    /* 3: resize grow with fill=0xCC */
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"ab\")\n"
        "b.resize(5, 0xCC)\n"
        "assert(b.len() == 5)\n"
        "assert(b.get(2) == 0xCC)\n"
        "assert(b.get(4) == 0xCC)"
    );
}

static void test_resize_shrink(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello\")\n"
        "b.resize(2)\n"
        "assert(b.len() == 2)\n"
        "assert(b.to_str() == \"he\")"
    );
}

/* ---- copy ---- */

static void test_copy_independence(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var a = buffer.from_str(\"hello\")\n"
        "var b = a.copy()\n"
        "b.set(0, 88)\n"   /* 'X' */
        "assert(a.get(0) == 104)\n"  /* original unchanged */
        "assert(b.get(0) == 88)"
    );
}

/* ---- to_str / to_hex ---- */

static void test_to_hex(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello\")\n"
        "assert(b.to_hex() == \"68656c6c6f\")"
    );
}

/* ---- find ---- */

static void test_find_found(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello world\")\n"
        "var sub = buffer.from_str(\"world\")\n"
        "assert(b.find(sub) == 6)"
    );
}

static void test_find_not_found(void) {
    /* 4: find not found returns -1 */
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello\")\n"
        "var sub = buffer.from_str(\"xyz\")\n"
        "assert(b.find(sub) == -1)"
    );
}

static void test_find_str_arg(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello world\")\n"
        "assert(b.find(\"world\") == 6)"
    );
}

static void test_find_with_start(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"aababc\")\n"
        "assert(b.find(\"ab\", 2) == 3)"
    );
}

/* ---- replace ---- */

static void test_replace_all(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"aababab\")\n"
        "var old = buffer.from_str(\"ab\")\n"
        "var nw  = buffer.from_str(\"X\")\n"
        "var r   = b.replace(old, nw)\n"
        "assert(r.to_str() == \"aXXX\")"
    );
}

static void test_replace_count_limit(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"ababab\")\n"
        "var old = buffer.from_str(\"ab\")\n"
        "var nw  = buffer.from_str(\"X\")\n"
        "var r   = b.replace(old, nw, 2)\n"
        "assert(r.to_str() == \"XXab\")"
    );
}

/* ---- equals ---- */

static void test_equals_true(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var a = buffer.from_str(\"hello\")\n"
        "var b = buffer.from_str(\"hello\")\n"
        "assert(a.equals(b) == true)"
    );
}

static void test_equals_false(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var a = buffer.from_str(\"hello\")\n"
        "var b = buffer.from_str(\"world\")\n"
        "assert(a.equals(b) == false)"
    );
}

/* ---- concat (method form) ---- */

static void test_concat_method(void) {
    RUN_OK(
        "import \"buffer\"\n"
        "var a = buffer.from_str(\"foo\")\n"
        "var b = buffer.from_str(\"bar\")\n"
        "var c = a.concat(b)\n"
        "assert(c.to_str() == \"foobar\")"
    );
}

/* ---- main ---- */

int main(void) {
    test_new_empty();
    test_new_with_size_and_fill();
    test_from_str();
    test_from_hex_basic();
    test_from_hex_odd_length_error();
    test_from_hex_uppercase();
    test_concat_static();
    test_get_negative_index();
    test_set_negative_index();
    test_get_out_of_bounds_returns_nil();
    test_slice();
    test_slice_end_minus1();
    test_append_str();
    test_append_buffer();
    test_prepend();
    test_fill();
    test_clear();
    test_resize_grow_fill();
    test_resize_shrink();
    test_copy_independence();
    test_to_hex();
    test_find_found();
    test_find_not_found();
    test_find_str_arg();
    test_find_with_start();
    test_replace_all();
    test_replace_count_limit();
    test_equals_true();
    test_equals_false();
    test_concat_method();
    printf("All buffer tests passed.\n");
    return 0;
}
