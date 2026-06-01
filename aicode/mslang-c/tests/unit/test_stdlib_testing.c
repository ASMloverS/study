/* tests/unit/test_stdlib_testing.c - STDLIB-45: testing module
 *
 * Verifies all public API functions via ms_vm_interpret.
 */
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
        fprintf(stderr, "FAIL test_stdlib_testing.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r == MS_INTERPRET_OK) {
        fprintf(stderr, "FAIL test_stdlib_testing.c:%d: expected runtime error\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- import smoke ---- */

static void test_import(void) {
    RUN_OK("import \"testing\"");
}

/* ---- assert passes when true ---- */

static void test_assert_pass(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.assert(true)"
    );
    RUN_OK(
        "import \"testing\"\n"
        "testing.assert(1 == 1, \"ok\")"
    );
}

/* ---- assert fails when false ---- */

static void test_assert_fail(void) {
    RUN_ERR(
        "import \"testing\"\n"
        "testing.assert(false)"
    );
    RUN_ERR(
        "import \"testing\"\n"
        "testing.assert(false, \"should fail\")"
    );
}

/* ---- assert_eq / assert_ne ---- */

static void test_assert_eq_ne(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.assert_eq(1, 1)\n"
        "testing.assert_eq(\"hi\", \"hi\")\n"
        "testing.assert_ne(1, 2)\n"
        "testing.assert_ne(\"a\", \"b\")"
    );
    RUN_ERR("import \"testing\"\ntesting.assert_eq(1, 2)");
    RUN_ERR("import \"testing\"\ntesting.assert_ne(1, 1)");
}

/* ---- assert_lt/le/gt/ge ---- */

static void test_assert_numeric(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.assert_lt(1, 2)\n"
        "testing.assert_le(1, 1)\n"
        "testing.assert_gt(2, 1)\n"
        "testing.assert_ge(2, 2)"
    );
    RUN_ERR("import \"testing\"\ntesting.assert_lt(2, 1)");
    RUN_ERR("import \"testing\"\ntesting.assert_gt(1, 2)");
}

/* ---- assert_str_eq ---- */

static void test_assert_str_eq(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.assert_str_eq(\"hello\", \"hello\")"
    );
    RUN_ERR(
        "import \"testing\"\n"
        "testing.assert_str_eq(\"hello\", \"world\")"
    );
}

/* ---- assert_approx ---- */

static void test_assert_approx(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.assert_approx(1.0 / 3.0, 0.3333333333, 1e-9)"
    );
    RUN_ERR(
        "import \"testing\"\n"
        "testing.assert_approx(1.0, 2.0, 1e-9)"
    );
}

/* ---- assert_raises / assert_no_raise ---- */

static void test_assert_raises(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.assert_raises(fun() { throw \"oops\" })"
    );
    RUN_ERR(
        "import \"testing\"\n"
        "testing.assert_raises(fun() { var x = 1 })"
    );
}

static void test_assert_no_raise(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.assert_no_raise(fun() { var x = 1 })"
    );
    RUN_ERR(
        "import \"testing\"\n"
        "testing.assert_no_raise(fun() { throw \"oops\" })"
    );
}

/* ---- run / run_all ---- */

static void test_run_all_pass(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.run(\"add\", fun() {\n"
        "    testing.assert_eq(1 + 1, 2)\n"
        "})\n"
        "var r = testing.run_all()\n"
        "assert(r[\"passed\"]  == 1)\n"
        "assert(r[\"failed\"]  == 0)\n"
        "assert(r[\"skipped\"] == 0)"
    );
}

static void test_run_all_fail(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.run(\"bad\", fun() {\n"
        "    testing.assert_eq(1, 2)\n"
        "})\n"
        "var r = testing.run_all()\n"
        "assert(r[\"passed\"] == 0)\n"
        "assert(r[\"failed\"] == 1)"
    );
}

/* ---- skip ---- */

static void test_skip(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.run(\"skipped\", fun() {\n"
        "    testing.skip(\"not yet\")\n"
        "    testing.assert(false)\n"
        "})\n"
        "var r = testing.run_all()\n"
        "assert(r[\"skipped\"] == 1)\n"
        "assert(r[\"failed\"]  == 0)"
    );
}

/* ---- fail (soft fail) ---- */

static void test_soft_fail(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.run(\"soft\", fun() {\n"
        "    testing.fail(\"manual fail\")\n"
        "})\n"
        "var r = testing.run_all()\n"
        "assert(r[\"failed\"] == 1)"
    );
}

/* ---- run_matching ---- */

static void test_run_matching(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.run(\"math_add\", fun() { testing.assert_eq(1 + 1, 2) })\n"
        "testing.run(\"math_sub\", fun() { testing.assert_eq(3 - 1, 2) })\n"
        "testing.run(\"str_len\",  fun() { testing.assert_eq(3, 3) })\n"
        "var r = testing.run_matching(\"math\")\n"
        "assert(r[\"passed\"] == 2)"
    );
}

/* ---- bench ---- */

static void test_bench(void) {
    RUN_OK(
        "import \"testing\"\n"
        "var r = testing.bench(\"noop\", fun() { var x = 1 + 1 }, 10)\n"
        "assert(r[\"n\"] == 10)\n"
        "assert(r[\"total_ms\"] >= 0)"
    );
}

/* ---- bench_all ---- */

static void test_bench_all(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.bench(\"a\", fun() { }, 5)\n"
        "testing.bench(\"b\", fun() { }, 5)\n"
        "var rs = testing.bench_all()\n"
        "assert(rs.len() == 2)"
    );
}

/* ---- errors list on failure ---- */

static void test_errors_list(void) {
    RUN_OK(
        "import \"testing\"\n"
        "testing.run(\"fail1\", fun() { testing.assert(false) })\n"
        "var r = testing.run_all()\n"
        "assert(r[\"errors\"].len() == 1)\n"
        "assert(r[\"errors\"][0][\"name\"] == \"fail1\")"
    );
}

int main(void) {
    test_import();
    test_assert_pass();
    test_assert_fail();
    test_assert_eq_ne();
    test_assert_numeric();
    test_assert_str_eq();
    test_assert_approx();
    test_assert_raises();
    test_assert_no_raise();
    test_run_all_pass();
    test_run_all_fail();
    test_skip();
    test_soft_fail();
    test_run_matching();
    test_bench();
    test_bench_all();
    test_errors_list();
    printf("test_stdlib_testing: all passed\n");
    return 0;
}
