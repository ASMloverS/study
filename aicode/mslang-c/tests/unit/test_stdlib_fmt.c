/* tests/unit/test_stdlib_fmt.c - STDLIB-15: fmt module */
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
        fprintf(stderr, "FAIL test_stdlib_fmt.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_fmt.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- sprintf basic integer specifiers ---- */
static void test_sprintf_basic(void) {
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%s\", \"hello\") == \"hello\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%d\", 42) == \"42\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%d\", -7) == \"-7\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%x\", 255) == \"ff\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%X\", 255) == \"FF\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%o\", 8) == \"10\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%b\", 10) == \"1010\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%%\") == \"%\")\n");
}

/* ---- sprintf float specifiers ---- */
static void test_sprintf_float(void) {
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%.2f\", 3.14159) == \"3.14\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%f\", 0.0) == \"0.000000\")\n");
    RUN_OK("import \"fmt\"\nvar s = fmt.sprintf(\"%e\", 12345.6789)\nassert(s != nil)\n");
    RUN_OK("import \"fmt\"\nvar s = fmt.sprintf(\"%g\", 100.0)\nassert(s != nil)\n");
}

/* ---- sprintf width and padding ---- */
static void test_sprintf_width(void) {
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%10d\", 42) == \"        42\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%-10d\", 42) == \"42        \")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%010d\", 42) == \"0000000042\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%+d\", 42) == \"+42\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%+d\", -42) == \"-42\")\n");
}

/* ---- sprintf %v and %c ---- */
static void test_sprintf_misc(void) {
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%v\", 42) != nil)\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%v\", true) != nil)\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%c\", 65) == \"A\")\n");
    RUN_OK("import \"fmt\"\nassert(fmt.sprintf(\"%c\", 97) == \"a\")\n");
}

/* ---- sprintf %q ---- */
static void test_sprintf_q(void) {
    RUN_OK("import \"fmt\"\nvar q = fmt.sprintf(\"%q\", \"hello\")\nassert(q == \"\\\"hello\\\"\")\n");
    RUN_OK("import \"fmt\"\nvar q = fmt.sprintf(\"%q\", \"a\\nb\")\nassert(q == \"\\\"a\\\\nb\\\"\")\n");
}

/* ---- sprintf multiple args ---- */
static void test_sprintf_multi(void) {
    RUN_OK(
        "import \"fmt\"\n"
        "var s = fmt.sprintf(\"%s is %d years old\", \"Alice\", 30)\n"
        "assert(s == \"Alice is 30 years old\")\n"
    );
    RUN_OK("import \"fmt\"\nvar s = fmt.sprintf(\"%.4f\", 3.14159)\nassert(s == \"3.1416\")\n");
}

/* ---- printf / println / eprintf smoke ---- */
static void test_printf_family(void) {
    RUN_OK("import \"fmt\"\nfmt.printf(\"%d\\n\", 1)\n");
    RUN_OK("import \"fmt\"\nfmt.println(\"%d\", 2)\n");
    RUN_OK("import \"fmt\"\nfmt.eprintf(\"%s\\n\", \"err\")\n");
}

/* ---- format(value) ---- */
static void test_format(void) {
    RUN_OK("import \"fmt\"\nvar s = fmt.format(42)\nassert(s == \"<int: 42>\")\n");
    RUN_OK("import \"fmt\"\nvar s = fmt.format(3.14)\nassert(s != nil)\n");
    RUN_OK("import \"fmt\"\nvar s = fmt.format(true)\nassert(s == \"<bool: true>\")\n");
    RUN_OK("import \"fmt\"\nvar s = fmt.format(nil)\nassert(s == \"<nil>\")\n");
    RUN_OK("import \"fmt\"\nvar s = fmt.format(\"hello\")\nassert(s == \"<string: hello>\")\n");
}

/* ---- error: not enough args ---- */
static void test_sprintf_errors(void) {
    RUN_ERR("import \"fmt\"\nfmt.sprintf(\"%d\")\n");
}

int main(void) {
    test_sprintf_basic();
    test_sprintf_float();
    test_sprintf_width();
    test_sprintf_misc();
    test_sprintf_q();
    test_sprintf_multi();
    test_printf_family();
    test_format();
    test_sprintf_errors();
    printf("test_stdlib_fmt: all passed\n");
    return 0;
}
