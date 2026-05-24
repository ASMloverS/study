/* tests/unit/test_stdlib_errors.c - STDLIB-17: errors module */
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
        fprintf(stderr, "FAIL test_stdlib_errors.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_errors.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- errors.new ---- */
static void test_new(void) {
    RUN_OK(
        "import \"errors\"\n"
        "var e = errors.new(\"oops\")\n"
        "assert(e != nil)\n"
    );
    RUN_OK(
        "import \"errors\"\n"
        "var e = errors.new(\"oops\")\n"
        "assert(errors.message(e) == \"oops\")\n"
    );
    /* cause is nil for a plain new error */
    RUN_OK(
        "import \"errors\"\n"
        "var e = errors.new(\"oops\")\n"
        "assert(errors.unwrap(e) == nil)\n"
    );
}

/* ---- errors.wrap ---- */
static void test_wrap(void) {
    RUN_OK(
        "import \"errors\"\n"
        "var root = errors.new(\"root\")\n"
        "var wrapped = errors.wrap(root, \"outer\")\n"
        "assert(errors.message(wrapped) == \"outer\")\n"
        "assert(errors.unwrap(wrapped) != nil)\n"
        "assert(errors.message(errors.unwrap(wrapped)) == \"root\")\n"
    );
}

/* ---- errors.is (identity chain search) ---- */
static void test_is(void) {
    RUN_OK(
        "import \"errors\"\n"
        "var ErrNotFound = errors.new(\"not found\")\n"
        "var wrapped = errors.wrap(ErrNotFound, \"op: path missing\")\n"
        "assert(errors.is(wrapped, ErrNotFound) == true)\n"
    );
    RUN_OK(
        "import \"errors\"\n"
        "var ErrA = errors.new(\"A\")\n"
        "var ErrB = errors.new(\"B\")\n"
        "assert(errors.is(ErrA, ErrB) == false)\n"
    );
    /* errors.is(e, e) should be true */
    RUN_OK(
        "import \"errors\"\n"
        "var e = errors.new(\"x\")\n"
        "assert(errors.is(e, e) == true)\n"
    );
}

/* ---- errors.chain ---- */
static void test_chain(void) {
    RUN_OK(
        "import \"errors\"\n"
        "var root = errors.new(\"root\")\n"
        "var mid  = errors.wrap(root, \"mid\")\n"
        "var top  = errors.wrap(mid, \"top\")\n"
        "var ch = errors.chain(top)\n"
        "assert(len(ch) == 3)\n"
        "assert(errors.message(ch[0]) == \"top\")\n"
        "assert(errors.message(ch[1]) == \"mid\")\n"
        "assert(errors.message(ch[2]) == \"root\")\n"
    );
    /* single error chain has length 1 */
    RUN_OK(
        "import \"errors\"\n"
        "var e = errors.new(\"only\")\n"
        "assert(len(errors.chain(e)) == 1)\n"
    );
}

/* ---- errors.format ---- */
static void test_format(void) {
    RUN_OK(
        "import \"errors\"\n"
        "var root    = errors.new(\"not found\")\n"
        "var wrapped = errors.wrap(root, \"read_file: empty path\")\n"
        "assert(errors.format(wrapped) == \"read_file: empty path: not found\")\n"
    );
    RUN_OK(
        "import \"errors\"\n"
        "var e = errors.new(\"single\")\n"
        "assert(errors.format(e) == \"single\")\n"
    );
}

/* ---- spec example (throw / catch) ---- */
static void test_throw_catch(void) {
    RUN_OK(
        "import \"errors\"\n"
        "var ErrNotFound = errors.new(\"not found\")\n"
        "fun read_file(path) {\n"
        "    if (path == \"\") {\n"
        "        throw errors.wrap(ErrNotFound, \"read_file: empty path\")\n"
        "    }\n"
        "}\n"
        "try {\n"
        "    read_file(\"\")\n"
        "} catch (e) {\n"
        "    assert(errors.message(e) == \"read_file: empty path\")\n"
        "    assert(errors.is(e, ErrNotFound) == true)\n"
        "    assert(errors.format(e) == \"read_file: empty path: not found\")\n"
        "    var ch = errors.chain(e)\n"
        "    assert(len(ch) == 2)\n"
        "}\n"
    );
}

/* ---- error: non-string arg to new ---- */
static void test_bad_args(void) {
    RUN_ERR("import \"errors\"\nerrors.new(42)\n");
    RUN_ERR("import \"errors\"\nerrors.wrap(nil, \"msg\")\n");
    RUN_ERR("import \"errors\"\nerrors.message(nil)\n");
    RUN_ERR("import \"errors\"\nerrors.unwrap(nil)\n");
    RUN_ERR("import \"errors\"\nerrors.is(nil, nil)\n");
    RUN_ERR("import \"errors\"\nerrors.chain(nil)\n");
    RUN_ERR("import \"errors\"\nerrors.format(nil)\n");
}

int main(void) {
    test_new();
    test_wrap();
    test_is();
    test_chain();
    test_format();
    test_throw_catch();
    test_bad_args();
    printf("test_stdlib_errors: all passed\n");
    return 0;
}
