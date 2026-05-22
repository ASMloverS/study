/* tests/unit/test_stdlib_gc.c - STDLIB-10: gc module */
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

/* ---- 1: collect returns >= 0 ---- */

static void test_collect_returns_int(void) {
    RUN_OK(
        "import \"gc\"\n"
        "var n = gc.collect()\n"
        "assert(n >= 0)\n"
    );
}

/* ---- 2: minor returns >= 0 ---- */

static void test_minor_returns_int(void) {
    RUN_OK(
        "import \"gc\"\n"
        "var n = gc.minor()\n"
        "assert(n >= 0)\n"
    );
}

/* ---- 3: pause / is_paused / resume ---- */

static void test_pause_resume(void) {
    RUN_OK(
        "import \"gc\"\n"
        "assert(!gc.is_paused())\n"
        "gc.pause()\n"
        "assert(gc.is_paused())\n"
        "gc.resume()\n"
        "assert(!gc.is_paused())\n"
    );
}

/* ---- 4: set_threshold clamps to >= 1024 ---- */

static void test_set_threshold_clamp(void) {
    RUN_OK(
        "import \"gc\"\n"
        "gc.set_threshold(100)\n"           /* below 1024, should be clamped */
        "assert(gc.threshold() == 1024)\n"
    );
}

static void test_set_threshold_exact(void) {
    RUN_OK(
        "import \"gc\"\n"
        "gc.set_threshold(1024 * 1024)\n"
        "assert(gc.threshold() == 1024 * 1024)\n"
    );
}

/* ---- 5: object_counts returns non-empty map ---- */

static void test_object_counts_nonempty(void) {
    RUN_OK(
        "import \"gc\"\n"
        "var m = gc.object_counts()\n"
        "assert(m != nil)\n"
    );
}

/* ---- 6: bytes_allocated and alive are non-negative ---- */

static void test_stats_non_negative(void) {
    RUN_OK(
        "import \"gc\"\n"
        "assert(gc.bytes_allocated() >= 0)\n"
        "assert(gc.alive() >= 0)\n"
    );
}

/* ---- 7: set_threshold with non-int errors ---- */

static void test_set_threshold_type_error(void) {
    RUN_ERR(
        "import \"gc\"\n"
        "gc.set_threshold(\"big\")\n"
    );
}

/* ---- main ---- */

int main(void) {
    test_collect_returns_int();
    test_minor_returns_int();
    test_pause_resume();
    test_set_threshold_clamp();
    test_set_threshold_exact();
    test_object_counts_nonempty();
    test_stats_non_negative();
    test_set_threshold_type_error();
    printf("test_stdlib_gc: all passed\n");
    return 0;
}
