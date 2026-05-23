/* tests/unit/test_stdlib_strings.c - STDLIB-13: strings module */
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

/* ---- join ---- */
static void test_join(void) {
    RUN_OK(
        "import \"strings\"\n"
        "var r = strings.join([\"a\", \"b\", \"c\"], \"-\")\n"
        "assert(r == \"a-b-c\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.join([], \",\") == \"\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.join([\"x\"], \",\") == \"x\")\n"
    );
}

/* ---- repeat ---- */
static void test_repeat(void) {
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.repeat(\"ab\", 3) == \"ababab\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.repeat(\"x\", 0) == \"\")\n"
    );
}

/* ---- fields ---- */
static void test_fields(void) {
    RUN_OK(
        "import \"strings\"\n"
        "var f = strings.fields(\"  foo  bar  \")\n"
        "assert(f[0] == \"foo\")\n"
        "assert(f[1] == \"bar\")\n"
        "assert(f.len() == 2)\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.fields(\"\").len() == 0)\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.fields(\"   \").len() == 0)\n"
    );
}

/* ---- split / split_n ---- */
static void test_split(void) {
    RUN_OK(
        "import \"strings\"\n"
        "var p = strings.split(\"a,b,c\", \",\")\n"
        "assert(p.len() == 3)\n"
        "assert(p[0] == \"a\" and p[2] == \"c\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "var p = strings.split_n(\"a,b,c,d\", \",\", 2)\n"
        "assert(p.len() == 2)\n"
        "assert(p[1] == \"b,c,d\")\n"
    );
}

/* ---- contains / has_prefix / has_suffix ---- */
static void test_predicates(void) {
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.contains(\"hello world\", \"world\"))\n"
        "assert(!strings.contains(\"hello\", \"xyz\"))\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.has_prefix(\"foobar\", \"foo\"))\n"
        "assert(!strings.has_prefix(\"foobar\", \"bar\"))\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.has_suffix(\"foobar\", \"bar\"))\n"
        "assert(!strings.has_suffix(\"foobar\", \"foo\"))\n"
    );
}

/* ---- index / last_index / count ---- */
static void test_search(void) {
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.index(\"hello world\", \"world\") == 6)\n"
        "assert(strings.index(\"hello\", \"xyz\") == -1)\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.last_index(\"abcabc\", \"bc\") == 4)\n"
        "assert(strings.last_index(\"hello\", \"xyz\") == -1)\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.count(\"aababab\", \"ab\") == 3)\n"
        "assert(strings.count(\"hello\", \"xyz\") == 0)\n"
    );
}

/* ---- replace / replace_all ---- */
static void test_replace(void) {
    RUN_OK(
        "import \"strings\"\n"
        "var s = \"hello world\"\n"
        "assert(strings.replace(s, \"o\", \"0\", 1) == \"hell0 world\")\n"
        "assert(strings.replace(s, \"o\", \"0\", -1) == \"hell0 w0rld\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.replace_all(\"hello world\", \"o\", \"0\") == \"hell0 w0rld\")\n"
    );
}

/* ---- trim variants ---- */
static void test_trim(void) {
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.trim(\"xxhelloxx\", \"x\") == \"hello\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.trim_left(\"xxhello\", \"x\") == \"hello\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.trim_right(\"helloxx\", \"x\") == \"hello\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.trim_space(\"  hello  \") == \"hello\")\n"
    );
}

/* ---- pad / center ---- */
static void test_pad(void) {
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.pad_left(\"42\", 6) == \"    42\")\n"
        "assert(strings.pad_left(\"42\", 6, \"0\") == \"000042\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.pad_right(\"42\", 6) == \"42    \")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "var c = strings.center(\"hi\", 6)\n"
        "assert(c == \"  hi  \")\n"
    );
}

/* ---- case ---- */
static void test_case(void) {
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.to_upper(\"hello\") == \"HELLO\")\n"
        "assert(strings.to_lower(\"HELLO\") == \"hello\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.title(\"hello world\") == \"Hello World\")\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.capitalize(\"hello world\") == \"Hello world\")\n"
    );
}

/* ---- char class ---- */
static void test_char_class(void) {
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.is_alpha(\"a\"))\n"
        "assert(!strings.is_alpha(\"1\"))\n"
        "assert(strings.is_digit(\"9\"))\n"
        "assert(!strings.is_digit(\"a\"))\n"
        "assert(strings.is_alnum(\"a\"))\n"
        "assert(strings.is_alnum(\"1\"))\n"
        "assert(!strings.is_alnum(\" \"))\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.is_space(\" \"))\n"
        "assert(strings.is_upper(\"A\"))\n"
        "assert(strings.is_lower(\"a\"))\n"
    );
}

/* ---- to_bytes / from_bytes ---- */
static void test_bytes(void) {
    RUN_OK(
        "import \"strings\"\n"
        "var b = strings.to_bytes(\"ABC\")\n"
        "assert(b[0] == 65 and b[1] == 66 and b[2] == 67)\n"
    );
    RUN_OK(
        "import \"strings\"\n"
        "var s = strings.from_bytes([72, 101, 108, 108, 111])\n"
        "assert(s == \"Hello\")\n"
    );
}

/* ---- to_buffer ---- */
static void test_to_buffer(void) {
    RUN_OK(
        "import \"strings\"\n"
        "var buf = strings.to_buffer(\"hi\")\n"
        "assert(buf != nil)\n"
    );
}

/* ---- is_empty ---- */
static void test_is_empty(void) {
    RUN_OK(
        "import \"strings\"\n"
        "assert(strings.is_empty(\"\"))\n"
        "assert(!strings.is_empty(\"x\"))\n"
    );
}

/* ---- main ---- */
int main(void) {
    test_join();
    test_repeat();
    test_fields();
    test_split();
    test_predicates();
    test_search();
    test_replace();
    test_trim();
    test_pad();
    test_case();
    test_char_class();
    test_bytes();
    test_to_buffer();
    test_is_empty();
    printf("test_stdlib_strings: all passed\n");
    return 0;
}
