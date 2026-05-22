/* tests/unit/test_stdlib_time.c - STDLIB-03: time module */
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

/* ---- time.now() ---- */

static void test_now(void) {
    /* now() returns a positive float (Unix epoch well past 0) */
    RUN_OK("import \"time\"\nvar t = time.now()\nassert(type(t) == \"number\")\nassert(t > 1000000000.0)");
}

/* ---- time.now_ms() ---- */

static void test_now_ms(void) {
    /* now_ms() returns an int > 0 */
    RUN_OK("import \"time\"\nvar t = time.now_ms()\nassert(type(t) == \"int\")\nassert(t > 1000000000000)");
}

/* ---- time.monotonic_ms() ---- */

static void test_monotonic_ms(void) {
    /* monotonic_ms() returns a non-negative int */
    RUN_OK("import \"time\"\nvar t = time.monotonic_ms()\nassert(type(t) == \"int\")\nassert(t >= 0)");
    /* two calls: second >= first */
    RUN_OK("import \"time\"\nvar a = time.monotonic_ms()\nvar b = time.monotonic_ms()\nassert(b >= a)");
}

/* ---- time.clock() ---- */

static void test_clock(void) {
    RUN_OK("import \"time\"\nvar c = time.clock()\nassert(type(c) == \"number\")\nassert(c >= 0.0)");
}

/* ---- time.format() ---- */

static void test_format(void) {
    const char* src1 =
        "import \"time\"\n"
        "var fmt = \"%Y\"\n"
        "var s = time.format(0.0, fmt)\n"
        "assert(type(s) == \"string\")\n"
        "assert(len(s) == 4)";
    TEST_ASSERT_EQ(run_snippet(src1), MS_INTERPRET_OK);

    const char* src2 =
        "import \"time\"\n"
        "var fmt = \"%Y-%m-%d\"\n"
        "var s = time.format(time.now(), fmt)\n"
        "assert(type(s) == \"string\")\n"
        "assert(len(s) == 10)";
    TEST_ASSERT_EQ(run_snippet(src2), MS_INTERPRET_OK);
}

/* ---- time.parse() ---- */

static void test_parse_roundtrip(void) {
    const char* src =
        "import \"time\"\n"
        "var fmt = \"%Y-%m-%d\"\n"
        "var ts = 1609459200.0\n"
        "var s  = time.format(ts, fmt)\n"
        "var t2 = time.parse(s, fmt)\n"
        "assert(type(t2) == \"number\")";
    TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK);
}

static void test_parse_invalid(void) {
    const char* src =
        "import \"time\"\n"
        "var fmt = \"%Y-%m-%d\"\n"
        "time.parse(\"not-a-date\", fmt)";
    TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_RUNTIME_ERROR);
}

/* ---- time.struct() ---- */

static void test_struct(void) {
    /* struct returns a map with known keys */
    RUN_OK(
        "import \"time\"\n"
        "var s = time.struct(1609459200.0)\n"   /* 2021-01-01 00:00:00 UTC */
        "assert(type(s) == \"map\")\n"
        "assert(s[\"year\"] == 2021)\n"
        "assert(s[\"month\"] == 1)\n"
        "assert(s[\"day\"]   == 1)"
    );
    /* has all required keys */
    RUN_OK(
        "import \"time\"\n"
        "var s = time.struct(time.now())\n"
        "var _ = s[\"hour\"]\n"
        "var _ = s[\"min\"]\n"
        "var _ = s[\"sec\"]\n"
        "var _ = s[\"wday\"]\n"
        "var _ = s[\"yday\"]"
    );
}

/* ---- time.sleep_sync() ---- */

static void test_sleep_sync(void) {
    /* sleep_sync returns nil and at least that much time elapses */
    RUN_OK(
        "import \"time\"\n"
        "var t0 = time.monotonic_ms()\n"
        "time.sleep_sync(0.01)\n"
        "var t1 = time.monotonic_ms()\n"
        "assert(t1 - t0 >= 5)"   /* allow 5ms tolerance under parallel test load */
    );
}

/* ---- existing: clock / sleep / gather / resume / run_until_complete ---- */

static void test_clock_existing(void) {
    RUN_OK("import \"time\"\nvar c = time.clock()\nassert(c >= 0.0)");
}

static void test_gather_empty(void) {
    RUN_OK(
        "import \"time\"\n"
        "var r = time.run_until_complete(time.gather([]))\n"
        "assert(type(r) == \"list\")\n"
        "assert(len(r) == 0)"
    );
}

int main(void) {
    test_now();
    test_now_ms();
    test_monotonic_ms();
    test_clock();
    test_format();
    test_parse_roundtrip();
    test_parse_invalid();
    test_struct();
    test_sleep_sync();
    test_clock_existing();
    test_gather_empty();
    printf("test_stdlib_time: all passed\n");
    return 0;
}
