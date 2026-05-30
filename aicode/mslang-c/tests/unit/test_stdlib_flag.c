/* tests/unit/test_stdlib_flag.c - STDLIB-39: flag module */
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
        fprintf(stderr, "FAIL test_stdlib_flag.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

/* ---- default values before parse ---- */
static void test_defaults(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var h = flag.string(\"host\", \"localhost\", \"addr\")\n"
        "var p = flag.int(\"port\", 80, \"port\")\n"
        "var v = flag.bool(\"verbose\", false, \"verbose\")\n"
        "var f = flag.float(\"ratio\", 1.5, \"ratio\")\n"
        "assert(h.value == \"localhost\")\n"
        "assert(p.value == 80)\n"
        "assert(v.value == false)\n"
        "assert(f.value == 1.5)\n"
        "assert(h.is_set() == false)\n"
    );
}

/* ---- parse_args --name value ---- */
static void test_parse_args_space(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var h = flag.string(\"host\", \"x\", \"addr\")\n"
        "var p = flag.int(\"port\", 0, \"port\")\n"
        "flag.parse_args([\"--host\", \"example.com\", \"--port\", \"443\"])\n"
        "assert(h.value == \"example.com\")\n"
        "assert(p.value == 443)\n"
        "assert(h.is_set() == true)\n"
        "assert(p.is_set() == true)\n"
    );
}

/* ---- parse_args --name=value ---- */
static void test_parse_args_equals(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var h = flag.string(\"host\", \"x\", \"addr\")\n"
        "var p = flag.int(\"port\", 0, \"port\")\n"
        "flag.parse_args([\"--host=example.com\", \"--port=8080\"])\n"
        "assert(h.value == \"example.com\")\n"
        "assert(p.value == 8080)\n"
    );
}

/* ---- bool flag without value ---- */
static void test_bool_flag_no_value(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var v = flag.bool(\"verbose\", false, \"v\")\n"
        "flag.parse_args([\"--verbose\"])\n"
        "assert(v.value == true)\n"
        "assert(v.is_set() == true)\n"
    );
}

/* ---- --no-name sets bool to false ---- */
static void test_bool_flag_no_prefix(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var v = flag.bool(\"verbose\", true, \"v\")\n"
        "flag.parse_args([\"--no-verbose\"])\n"
        "assert(v.value == false)\n"
        "assert(v.is_set() == true)\n"
    );
}

/* ---- positional args ---- */
static void test_positional_args(void) {
    RUN_OK(
        "import \"flag\"\n"
        "flag.parse_args([\"a\", \"b\", \"c\"])\n"
        "var a = flag.args()\n"
        "assert(a.len() == 3)\n"
        "assert(a[0] == \"a\")\n"
        "assert(a[2] == \"c\")\n"
    );
}

/* ---- -- stops parsing ---- */
static void test_double_dash(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var p = flag.int(\"port\", 0, \"port\")\n"
        "flag.parse_args([\"--port\", \"9090\", \"--\", \"--not-a-flag\"])\n"
        "assert(p.value == 9090)\n"
        "var a = flag.args()\n"
        "assert(a.len() == 1)\n"
        "assert(a[0] == \"--not-a-flag\")\n"
    );
}

/* ---- mixed flags and positionals ---- */
static void test_mixed(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var v = flag.bool(\"verbose\", false, \"v\")\n"
        "var h = flag.string(\"host\", \"x\", \"h\")\n"
        "flag.parse_args([\"--verbose\", \"file1\", \"--host=srv\", \"file2\"])\n"
        "assert(v.value == true)\n"
        "assert(h.value == \"srv\")\n"
        "var a = flag.args()\n"
        "assert(a.len() == 2)\n"
        "assert(a[0] == \"file1\")\n"
        "assert(a[1] == \"file2\")\n"
    );
}

/* ---- float flag ---- */
static void test_float_flag(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var r = flag.float(\"ratio\", 0.0, \"ratio\")\n"
        "flag.parse_args([\"--ratio=3.14\"])\n"
        "assert(r.value > 3.13)\n"
        "assert(r.value < 3.15)\n"
    );
}

/* ---- single-dash short flag ---- */
static void test_single_dash(void) {
    RUN_OK(
        "import \"flag\"\n"
        "var v = flag.bool(\"v\", false, \"verbose\")\n"
        "flag.parse_args([\"-v\"])\n"
        "assert(v.value == true)\n"
    );
}

/* ---- usage() returns non-empty string ---- */
static void test_usage(void) {
    RUN_OK(
        "import \"flag\"\n"
        "flag.string(\"host\", \"localhost\", \"server address\")\n"
        "var u = flag.usage()\n"
        "assert(u.len() > 0)\n"
    );
}

int main(void) {
    test_defaults();
    test_parse_args_space();
    test_parse_args_equals();
    test_bool_flag_no_value();
    test_bool_flag_no_prefix();
    test_positional_args();
    test_double_dash();
    test_mixed();
    test_float_flag();
    test_single_dash();
    test_usage();
    printf("test_stdlib_flag: all passed\n");
    return 0;
}
