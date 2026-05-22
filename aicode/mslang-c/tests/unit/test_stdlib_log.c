/* tests/unit/test_stdlib_log.c - STDLIB-07: log module */
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

/* ---- 1: import and basic info/warn don't error ---- */

static void test_basic_import(void) {
    RUN_OK("import \"log\"\nlog.info(\"hello world\")");
}

static void test_all_levels_callable(void) {
    RUN_OK(
        "import \"log\"\n"
        "log.set_fatal_exits(false)\n"
        "log.set_level(\"trace\")\n"
        "log.trace(\"t\")\n"
        "log.debug(\"d\")\n"
        "log.info(\"i\")\n"
        "log.warn(\"w\")\n"
        "log.error(\"e\")\n"
        "log.fatal(\"f\")\n"
    );
}

/* ---- 2: set_level / get_level ---- */

static void test_set_get_level_int(void) {
    RUN_OK(
        "import \"log\"\n"
        "log.set_level(3)\n"
        "assert(log.get_level() == \"WARN\")"
    );
}

static void test_set_get_level_str(void) {
    RUN_OK(
        "import \"log\"\n"
        "log.set_level(\"error\")\n"
        "assert(log.get_level() == \"ERROR\")\n"
        "log.set_level(2)\n" /* reset to INFO */
    );
}

static void test_set_level_bad_string_errors(void) {
    RUN_ERR(
        "import \"log\"\n"
        "log.set_level(\"verbose\")"
    );
}

static void test_set_level_bad_int_errors(void) {
    RUN_ERR(
        "import \"log\"\n"
        "log.set_level(99)"
    );
}

/* ---- 3: messages below level are silently dropped ---- */

static void test_below_level_no_error(void) {
    /* set level to WARN; trace and debug should silently pass (not crash) */
    RUN_OK(
        "import \"log\"\n"
        "log.set_level(\"warn\")\n"
        "log.trace(\"hidden\")\n"
        "log.debug(\"hidden\")\n"
        "log.info(\"hidden\")\n"
        "log.set_level(2)\n"   /* reset */
    );
}

/* ---- 4: set_format changes the output template ---- */

static void test_set_format_no_error(void) {
    RUN_OK(
        "import \"log\"\n"
        "log.set_format(\"{level}: {msg}\")\n"
        "log.info(\"formatted\")\n"
        "log.set_format(\"{time} [{level}] {msg}\")\n" /* reset */
    );
}

/* ---- 5: fatal with set_fatal_exits(false) does not exit ---- */

static void test_fatal_no_exit(void) {
    RUN_OK(
        "import \"log\"\n"
        "log.set_fatal_exits(false)\n"
        "log.fatal(\"boom\")\n"
        "log.set_fatal_exits(false)\n" /* leave false so harness doesn't die */
    );
}

/* ---- 6: set_sink bad argument errors ---- */

static void test_set_sink_bad_arg(void) {
    RUN_ERR(
        "import \"log\"\n"
        "log.set_sink(42)"
    );
}

static void test_set_sink_nil_resets(void) {
    RUN_OK(
        "import \"log\"\n"
        "log.set_sink(nil)\n"
        "log.info(\"back to stderr\")"
    );
}

/* ---- 7: tag sub-logger via log.with() ---- */

static void test_with_creates_logger(void) {
    RUN_OK(
        "import \"log\"\n"
        "var lg = log.with(\"myapp\")\n"
        "assert(lg != nil)"
    );
}

static void test_sub_logger_info(void) {
    RUN_OK(
        "import \"log\"\n"
        "var lg = log.with(\"svc\")\n"
        "lg.info(\"tagged message\")"
    );
}

static void test_sub_logger_all_levels(void) {
    RUN_OK(
        "import \"log\"\n"
        "log.set_fatal_exits(false)\n"
        "log.set_level(\"trace\")\n"
        "var lg = log.with(\"t\")\n"
        "lg.trace(\"a\")\n"
        "lg.debug(\"b\")\n"
        "lg.info(\"c\")\n"
        "lg.warn(\"d\")\n"
        "lg.error(\"e\")\n"
        "lg.fatal(\"f\")\n"
        "log.set_level(2)\n"
    );
}

static void test_sub_logger_set_level(void) {
    RUN_OK(
        "import \"log\"\n"
        "var lg = log.with(\"app\")\n"
        "lg.set_level(\"error\")\n"
        "lg.debug(\"silent\")\n"   /* below error, no crash */
        "lg.error(\"visible\")\n"
    );
}

/* ---- 8: format specifiers ---- */

static void test_format_s(void) {
    /* Use variable to avoid MSVC /analyze treating #src as a format string */
    const char* src = "import \"log\"\nlog.info(\"hello %s\", \"world\")";
    TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK);
}

static void test_format_d(void) {
    const char* src = "import \"log\"\nlog.info(\"count=%d\", 42)";
    TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK);
}

static void test_format_f(void) {
    const char* src = "import \"log\"\nlog.info(\"pi=%f\", 3.14)";
    TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK);
}

static void test_format_v(void) {
    const char* src = "import \"log\"\nlog.info(\"val=%v\", true)";
    TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK);
}

/* ---- main ---- */

int main(void) {
    test_basic_import();
    test_all_levels_callable();
    test_set_get_level_int();
    test_set_get_level_str();
    test_set_level_bad_string_errors();
    test_set_level_bad_int_errors();
    test_below_level_no_error();
    test_set_format_no_error();
    test_fatal_no_exit();
    test_set_sink_bad_arg();
    test_set_sink_nil_resets();
    test_with_creates_logger();
    test_sub_logger_info();
    test_sub_logger_all_levels();
    test_sub_logger_set_level();
    test_format_s();
    test_format_d();
    test_format_f();
    test_format_v();
    printf("All log tests passed.\n");
    return 0;
}
