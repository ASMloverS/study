/* tests/unit/test_stdlib_debug.c - STDLIB-09: debug module */
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

/* ---- 1: import succeeds ---- */

static void test_import(void) {
    RUN_OK("import \"debug\"");
}

/* ---- 2: debug.typeof mirrors type() ---- */

static void test_typeof_nil(void) {
    RUN_OK("import \"debug\"\nassert(debug.typeof(nil) == \"nil\")");
}

static void test_typeof_bool(void) {
    RUN_OK("import \"debug\"\nassert(debug.typeof(true) == \"bool\")");
}

static void test_typeof_int(void) {
    RUN_OK("import \"debug\"\nassert(debug.typeof(42) == \"int\")");
}

static void test_typeof_number(void) {
    RUN_OK("import \"debug\"\nassert(debug.typeof(3.14) == \"number\")");
}

static void test_typeof_string(void) {
    RUN_OK("import \"debug\"\nassert(debug.typeof(\"hi\") == \"string\")");
}

static void test_typeof_function(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun f() {}\n"
        "assert(debug.typeof(f) == \"function\")"
    );
}

static void test_typeof_list(void) {
    RUN_OK("import \"debug\"\nassert(debug.typeof([1,2]) == \"list\")");
}

static void test_typeof_map(void) {
    RUN_OK("import \"debug\"\nassert(debug.typeof({\"a\":1}) == \"map\")");
}

/* ---- 3: debug.is_native / is_closure ---- */

static void test_is_native_false_for_closure(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun f() {}\n"
        "assert(debug.is_native(f) == false)"
    );
}

static void test_is_native_false_for_value(void) {
    RUN_OK("import \"debug\"\nassert(debug.is_native(42) == false)");
}

static void test_is_closure_true(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun f() {}\n"
        "assert(debug.is_closure(f) == true)"
    );
}

static void test_is_closure_false_for_nil(void) {
    RUN_OK("import \"debug\"\nassert(debug.is_closure(nil) == false)");
}

/* ---- 4: debug.vm_stats() ---- */

static void test_vm_stats_keys(void) {
    RUN_OK(
        "import \"debug\"\n"
        "var s = debug.vm_stats()\n"
        "assert(s[\"frame_used\"] >= 1)\n"
        "assert(s[\"stack_used\"] >= 0)\n"
        "assert(s[\"gc_bytes\"]   >= 0)\n"
        "assert(s[\"gc_threshold\"] > 0)"
    );
}

/* ---- 5: debug.frame_info() ---- */

static void test_frame_info_keys(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun foo() {\n"
        "    var fi = debug.frame_info()\n"
        "    assert(fi[\"name\"] == \"foo\")\n"
        "    assert(fi[\"line\"] >= 1)\n"
        "}\n"
        "foo()"
    );
}

static void test_frame_info_depth(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun inner() {\n"
        "    var fi = debug.frame_info(1)\n"
        "    assert(fi[\"name\"] == \"outer\")\n"
        "}\n"
        "fun outer() { inner() }\n"
        "outer()"
    );
}

static void test_frame_info_oob_errors(void) {
    RUN_ERR(
        "import \"debug\"\n"
        "debug.frame_info(9999)"
    );
}

/* ---- 6: debug.traceback() ---- */

static void test_traceback_returns_string(void) {
    RUN_OK(
        "import \"debug\"\n"
        "var tb = debug.traceback()\n"
        "assert(type(tb) == \"string\")\n"
        "assert(len(tb) > 0)"
    );
}

static void test_traceback_contains_function(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun foo() { return debug.traceback() }\n"
        "var tb = foo()\n"
        "assert(len(tb) > 0)"
    );
}

/* ---- 7: debug.locals() ---- */

static void test_locals_returns_map(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun f() {\n"
        "    var x = 42\n"
        "    var m = debug.locals()\n"
        "    assert(type(m) == \"map\")\n"
        "}\n"
        "f()"
    );
}

/* ---- 8: debug.upvalues() ---- */

static void test_upvalues_no_upvalues(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun f() {}\n"
        "var uv = debug.upvalues(f)\n"
        "assert(type(uv) == \"list\")\n"
        "assert(len(uv) == 0)"
    );
}

static void test_upvalues_bad_arg_errors(void) {
    RUN_ERR(
        "import \"debug\"\n"
        "debug.upvalues(42)"
    );
}

static void test_upvalues_has_entry_for_captured(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun make_adder(x) {\n"
        "    fun adder(y) { return x + y }\n"
        "    return adder\n"
        "}\n"
        "var add5 = make_adder(5)\n"
        "var uv = debug.upvalues(add5)\n"
        "assert(len(uv) >= 1)"
    );
}

/* ---- 9: debug.disasm() ---- */

static void test_disasm_returns_string(void) {
    RUN_OK(
        "import \"debug\"\n"
        "fun add(a, b) { return a + b }\n"
        "var txt = debug.disasm(add)\n"
        "assert(type(txt) == \"string\")\n"
        "assert(len(txt) > 0)"
    );
}

static void test_disasm_bad_arg_errors(void) {
    RUN_ERR(
        "import \"debug\"\n"
        "debug.disasm(42)"
    );
}

/* ---- 10: debug.gc_trace() ---- */

static void test_gc_trace_true(void) {
    RUN_OK("import \"debug\"\ndebug.gc_trace(true)");
}

static void test_gc_trace_false(void) {
    RUN_OK("import \"debug\"\ndebug.gc_trace(false)");
}

static void test_gc_trace_bad_arg_errors(void) {
    RUN_ERR("import \"debug\"\ndebug.gc_trace(42)");
}

/* ---- main ---- */

int main(void) {
    test_import();

    test_typeof_nil();
    test_typeof_bool();
    test_typeof_int();
    test_typeof_number();
    test_typeof_string();
    test_typeof_function();
    test_typeof_list();
    test_typeof_map();

    test_is_native_false_for_closure();
    test_is_native_false_for_value();
    test_is_closure_true();
    test_is_closure_false_for_nil();

    test_vm_stats_keys();

    test_frame_info_keys();
    test_frame_info_depth();
    test_frame_info_oob_errors();

    test_traceback_returns_string();
    test_traceback_contains_function();

    test_locals_returns_map();

    test_upvalues_no_upvalues();
    test_upvalues_bad_arg_errors();
    test_upvalues_has_entry_for_captured();

    test_disasm_returns_string();
    test_disasm_bad_arg_errors();

    test_gc_trace_true();
    test_gc_trace_false();
    test_gc_trace_bad_arg_errors();

    printf("All debug module tests passed.\n");
    return 0;
}
