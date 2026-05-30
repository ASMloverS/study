/* tests/unit/test_stdlib_context.c - STDLIB-42: context module */
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
        fprintf(stderr, "FAIL test_stdlib_context.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

/* ---- background: never cancelled ---- */
static void test_background(void) {
    RUN_OK(
        "import \"context\"\n"
        "var ctx = context.background()\n"
        "assert(!ctx.is_cancelled())\n"
        "assert(ctx.deadline() == nil)\n"
        "assert(ctx.value(\"x\") == nil)\n"
    );
}

/* ---- with_cancel: manual cancel ---- */
static void test_with_cancel(void) {
    RUN_OK(
        "import \"context\"\n"
        "var bg = context.background()\n"
        "var ctx = context.with_cancel(bg)\n"
        "assert(!ctx.is_cancelled())\n"
        "ctx.cancel()\n"
        "assert(ctx.is_cancelled())\n"
    );
    /* cancel is idempotent */
    RUN_OK(
        "import \"context\"\n"
        "var bg = context.background()\n"
        "var ctx = context.with_cancel(bg)\n"
        "ctx.cancel()\n"
        "ctx.cancel()\n"
        "assert(ctx.is_cancelled())\n"
    );
}

/* ---- child inherits parent cancellation ---- */
static void test_parent_cancel_propagates(void) {
    RUN_OK(
        "import \"context\"\n"
        "var bg  = context.background()\n"
        "var p   = context.with_cancel(bg)\n"
        "var c   = context.with_cancel(p)\n"
        "var gc  = context.with_cancel(c)\n"
        "assert(!gc.is_cancelled())\n"
        "p.cancel()\n"
        "assert(p.is_cancelled())\n"
        "assert(c.is_cancelled())\n"
        "assert(gc.is_cancelled())\n"
    );
}

/* ---- with_value: key lookup walks chain ---- */
static void test_with_value(void) {
    RUN_OK(
        "import \"context\"\n"
        "var bg  = context.background()\n"
        "var c1  = context.with_value(bg, \"user\", \"alice\")\n"
        "var c2  = context.with_value(c1, \"req\", 42)\n"
        "assert(c2.value(\"user\") == \"alice\")\n"
        "assert(c2.value(\"req\")  == 42)\n"
        "assert(c2.value(\"missing\") == nil)\n"
    );
    /* with_value ctx is not cancellable by cancel() on itself,
       but parent cancel still propagates */
    RUN_OK(
        "import \"context\"\n"
        "var bg  = context.background()\n"
        "var p   = context.with_cancel(bg)\n"
        "var vc  = context.with_value(p, \"k\", 1)\n"
        "assert(!vc.is_cancelled())\n"
        "p.cancel()\n"
        "assert(vc.is_cancelled())\n"
    );
}

/* ---- with_timeout: done() future resolves on cancel ---- */
static void test_done_future(void) {
    RUN_OK(
        "import \"context\"\n"
        "import \"time\"\n"
        "async fun main() {\n"
        "    var bg  = context.background()\n"
        "    var ctx = context.with_cancel(bg)\n"
        "    var done = ctx.done()\n"
        "    ctx.cancel()\n"
        "    await done\n"
        "    assert(ctx.is_cancelled())\n"
        "    print(\"done_ok\")\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
}

/* ---- with_timeout: auto-cancels after delay ---- */
static void test_with_timeout(void) {
    RUN_OK(
        "import \"context\"\n"
        "import \"time\"\n"
        "async fun main() {\n"
        "    var bg  = context.background()\n"
        "    var ctx = context.with_timeout(bg, 30)\n"
        "    assert(!ctx.is_cancelled())\n"
        "    await time.sleep(60)\n"
        "    assert(ctx.is_cancelled())\n"
        "    print(\"timeout_ok\")\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
}

/* ---- deadline() returns the correct value ---- */
static void test_deadline(void) {
    RUN_OK(
        "import \"context\"\n"
        "var bg = context.background()\n"
        "assert(bg.deadline() == nil)\n"
        "var ctx = context.with_timeout(bg, 1000)\n"
        "assert(ctx.deadline() != nil)\n"
    );
}

int main(void) {
    test_background();
    test_with_cancel();
    test_parent_cancel_propagates();
    test_with_value();
    test_done_future();
    test_with_timeout();
    test_deadline();
    printf("test_stdlib_context: all passed\n");
    return 0;
}
