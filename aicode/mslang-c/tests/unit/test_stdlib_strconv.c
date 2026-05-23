/* tests/unit/test_stdlib_strconv.c - STDLIB-14: strconv module */
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

/* ---- format_int ---- */
static void test_format_int(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.format_int(255, 16) == \"ff\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.format_int(10, 2) == \"1010\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.format_int(42, 10) == \"42\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.format_int(-1, 10) == \"-1\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.format_int(0, 10) == \"0\")\n"
    );
}

/* ---- parse_int ---- */
static void test_parse_int(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.parse_int(\"ff\", 16) == 255)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.parse_int(\"1010\", 2) == 10)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.parse_int(\"42\", 10) == 42)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.parse_int(\"-7\", 10) == -7)\n"
    );
    /* invalid → runtime error */
    RUN_ERR(
        "import \"strconv\"\n"
        "strconv.parse_int(\"bad\", 10)\n"
    );
}

/* ---- try_parse_int ---- */
static void test_try_parse_int(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.try_parse_int(\"42\", 10) == 42)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.try_parse_int(\"bad\", 10) == nil)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.try_parse_int(\"\", 10) == nil)\n"
    );
}

/* ---- format_float ---- */
static void test_format_float(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.format_float(3.14, \"f\", 2) == \"3.14\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "var s = strconv.format_float(1.0, \"e\", 2)\n"
        "assert(s != nil)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.format_float(0.0, \"f\", 1) == \"0.0\")\n"
    );
}

/* ---- parse_float ---- */
static void test_parse_float(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "var x = strconv.parse_float(\"3.14\")\n"
        "assert(x > 3.13 and x < 3.15)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.parse_float(\"0.0\") == 0.0)\n"
    );
    RUN_ERR(
        "import \"strconv\"\n"
        "strconv.parse_float(\"bad\")\n"
    );
}

/* ---- try_parse_float ---- */
static void test_try_parse_float(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "var x = strconv.try_parse_float(\"1.5\")\n"
        "assert(x > 1.4 and x < 1.6)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.try_parse_float(\"bad\") == nil)\n"
    );
}

/* ---- parse_bool ---- */
static void test_parse_bool(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.parse_bool(\"true\") == true)\n"
        "assert(strconv.parse_bool(\"1\") == true)\n"
        "assert(strconv.parse_bool(\"yes\") == true)\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.parse_bool(\"false\") == false)\n"
        "assert(strconv.parse_bool(\"0\") == false)\n"
        "assert(strconv.parse_bool(\"no\") == false)\n"
    );
    RUN_ERR(
        "import \"strconv\"\n"
        "strconv.parse_bool(\"maybe\")\n"
    );
}

/* ---- format_bool ---- */
static void test_format_bool(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.format_bool(true) == \"true\")\n"
        "assert(strconv.format_bool(false) == \"false\")\n"
    );
}

/* ---- quote / unquote ---- */
static void test_quote_unquote(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "var q = strconv.quote(\"hello\")\n"
        "assert(q == \"\\\"hello\\\"\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "var q = strconv.quote(\"a\\nb\")\n"
        "assert(q == \"\\\"a\\\\nb\\\"\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "var u = strconv.unquote(\"\\\"hello\\\"\")\n"
        "assert(u == \"hello\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "var u = strconv.unquote(\"\\\"a\\\\nb\\\"\")\n"
        "assert(u == \"a\\nb\")\n"
    );
    /* unquote without surrounding quotes → error */
    RUN_ERR(
        "import \"strconv\"\n"
        "strconv.unquote(\"hello\")\n"
    );
}

/* ---- quote_rune ---- */
static void test_quote_rune(void) {
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.quote_rune(\"a\") == \"'a'\")\n"
    );
    RUN_OK(
        "import \"strconv\"\n"
        "assert(strconv.quote_rune(\"\\n\") == \"'\\\\n'\")\n"
    );
}

int main(void) {
    test_format_int();
    test_parse_int();
    test_try_parse_int();
    test_format_float();
    test_parse_float();
    test_try_parse_float();
    test_parse_bool();
    test_format_bool();
    test_quote_unquote();
    test_quote_rune();
    printf("test_stdlib_strconv: all passed\n");
    return 0;
}
