/* tests/unit/test_stdlib_csv.c - STDLIB-33: csv module */
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
        fprintf(stderr, "FAIL test_stdlib_csv.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_csv.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- read_str basic ---- */
static void test_read_str_basic(void) {
    RUN_OK(
        "import \"csv\"\n"
        "var rows = csv.read_str(\"a,b,c\\n1,2,3\\n\")\n"
        "assert(rows.len() == 2)\n"
        "assert(rows[0][0] == \"a\")\n"
        "assert(rows[0][1] == \"b\")\n"
        "assert(rows[0][2] == \"c\")\n"
        "assert(rows[1][0] == \"1\")\n"
        "assert(rows[1][1] == \"2\")\n"
        "assert(rows[1][2] == \"3\")\n"
    );
}

/* ---- read_str with quotes ---- */
static void test_read_str_quoted(void) {
    RUN_OK(
        "import \"csv\"\n"
        "var rows = csv.read_str(\"\\\"hello world\\\",b\\n\")\n"
        "assert(rows.len() == 1)\n"
        "assert(rows[0][0] == \"hello world\")\n"
        "assert(rows[0][1] == \"b\")\n"
    );
    /* doubled quote inside quoted field: "a""b",c  →  a"b  |  c */
    RUN_OK(
        "import \"csv\"\n"
        "var rows = csv.read_str(\"\\\"a\\\"\\\"b\\\",c\\n\")\n"
        "assert(rows[0][0] == \"a\\\"b\")\n"
        "assert(rows[0][1] == \"c\")\n"
    );
}

/* ---- read_str with custom separator ---- */
static void test_read_str_sep(void) {
    RUN_OK(
        "import \"csv\"\n"
        "var rows = csv.read_str(\"a;b;c\\n1;2;3\\n\", \";\")\n"
        "assert(rows[0][0] == \"a\")\n"
        "assert(rows[0][2] == \"c\")\n"
        "assert(rows[1][1] == \"2\")\n"
    );
}

/* ---- write_str basic ---- */
static void test_write_str_basic(void) {
    RUN_OK(
        "import \"csv\"\n"
        "var s = csv.write_str([[\"name\",\"age\"],[\"Alice\",\"30\"]])\n"
        "assert(s == \"name,age\\nAlice,30\\n\")\n"
    );
}

/* ---- write_str: field containing separator gets quoted ---- */
static void test_write_str_quoting(void) {
    RUN_OK(
        "import \"csv\"\n"
        "var s = csv.write_str([[\"a,b\",\"c\"]])\n"
        "assert(s == \"\\\"a,b\\\",c\\n\")\n"
    );
}

/* ---- write_str with custom separator ---- */
static void test_write_str_sep(void) {
    RUN_OK(
        "import \"csv\"\n"
        "var s = csv.write_str([[\"x\",\"y\"]], \";\")\n"
        "assert(s == \"x;y\\n\")\n"
    );
}

/* ---- round-trip: write then read ---- */
static void test_roundtrip(void) {
    RUN_OK(
        "import \"csv\"\n"
        "var orig = [[\"name\",\"score\"],[\"Alice\",\"100\"],[\"Bob\",\"99\"]]\n"
        "var s = csv.write_str(orig)\n"
        "var back = csv.read_str(s)\n"
        "assert(back.len() == 3)\n"
        "assert(back[0][0] == \"name\")\n"
        "assert(back[1][1] == \"100\")\n"
        "assert(back[2][0] == \"Bob\")\n"
    );
}

/* ---- error cases ---- */
static void test_errors(void) {
    RUN_ERR("import \"csv\"\ncvs_read_str(42)\n");
    RUN_ERR("import \"csv\"\ncsv.write_str(42)\n");
    RUN_ERR("import \"csv\"\ncsv.read_str(42)\n");
}

int main(void) {
    test_read_str_basic();
    test_read_str_quoted();
    test_read_str_sep();
    test_write_str_basic();
    test_write_str_quoting();
    test_write_str_sep();
    test_roundtrip();
    test_errors();
    printf("test_stdlib_csv: all tests passed\n");
    return 0;
}
