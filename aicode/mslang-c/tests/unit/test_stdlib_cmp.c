/* tests/unit/test_stdlib_cmp.c - STDLIB-35: cmp module */
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
        fprintf(stderr, "FAIL test_stdlib_cmp.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

/* ---- compare ---- */
static void test_compare(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "assert(cmp.compare(1, 2) == -1)\n"
        "assert(cmp.compare(2, 1) == 1)\n"
        "assert(cmp.compare(3, 3) == 0)\n"
    );
    RUN_OK(
        "import \"cmp\"\n"
        "assert(cmp.compare(\"a\", \"b\") == -1)\n"
        "assert(cmp.compare(\"b\", \"a\") == 1)\n"
        "assert(cmp.compare(\"x\", \"x\") == 0)\n"
    );
}

/* ---- less / greater / equal ---- */
static void test_predicates(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "assert(cmp.less(1, 2) == true)\n"
        "assert(cmp.less(2, 1) == false)\n"
        "assert(cmp.greater(3, 2) == true)\n"
        "assert(cmp.greater(1, 5) == false)\n"
        "assert(cmp.equal(4, 4) == true)\n"
        "assert(cmp.equal(4, 5) == false)\n"
    );
}

/* ---- min / max ---- */
static void test_min_max(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "assert(cmp.min(3, 7) == 3)\n"
        "assert(cmp.min(9, 2) == 2)\n"
        "assert(cmp.max(3, 7) == 7)\n"
        "assert(cmp.max(9, 2) == 9)\n"
    );
    RUN_OK(
        "import \"cmp\"\n"
        "assert(cmp.min(\"apple\", \"banana\") == \"apple\")\n"
        "assert(cmp.max(\"apple\", \"banana\") == \"banana\")\n"
    );
}

/* ---- clamp ---- */
static void test_clamp(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "assert(cmp.clamp(5,  0, 10) == 5)\n"
        "assert(cmp.clamp(-3, 0, 10) == 0)\n"
        "assert(cmp.clamp(15, 0, 10) == 10)\n"
        "assert(cmp.clamp(0,  0, 10) == 0)\n"
        "assert(cmp.clamp(10, 0, 10) == 10)\n"
    );
}

/* ---- natural ---- */
static void test_natural(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "var fn = cmp.natural()\n"
        "assert(fn(1, 2) == -1)\n"
        "assert(fn(2, 2) == 0)\n"
        "assert(fn(3, 2) == 1)\n"
    );
}

/* ---- reversed ---- */
static void test_reversed(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "var rev = cmp.reversed(cmp.compare)\n"
        "assert(rev(1, 2) == 1)\n"
        "assert(rev(2, 1) == -1)\n"
        "assert(rev(3, 3) == 0)\n"
    );
    /* Double-reverse restores original order */
    RUN_OK(
        "import \"cmp\"\n"
        "var fn = cmp.reversed(cmp.reversed(cmp.compare))\n"
        "assert(fn(1, 2) == -1)\n"
        "assert(fn(2, 1) == 1)\n"
    );
}

/* ---- by_key ---- */
static void test_by_key(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "var by_len = cmp.by_key(fun(s) { return s.len() })\n"
        "assert(by_len(\"hi\", \"hello\") == -1)\n"
        "assert(by_len(\"abc\", \"ab\") == 1)\n"
        "assert(by_len(\"ab\", \"cd\") == 0)\n"
    );
}

/* ---- chain ---- */
static void test_chain(void) {
    /* Sort people by age then name */
    RUN_OK(
        "import \"cmp\"\n"
        "import \"sort\"\n"
        "var data = [\n"
        "    {\"name\": \"Bob\",   \"age\": 30},\n"
        "    {\"name\": \"Alice\", \"age\": 25},\n"
        "    {\"name\": \"Carol\", \"age\": 30}\n"
        "]\n"
        "var by_age  = cmp.by_key(fun(p) { return p[\"age\"]  })\n"
        "var by_name = cmp.by_key(fun(p) { return p[\"name\"] })\n"
        "var fn = cmp.chain([by_age, by_name])\n"
        "sort.sort(data, nil, false, fn)\n"
        "assert(data[0][\"name\"] == \"Alice\")\n"
        "assert(data[1][\"name\"] == \"Bob\")\n"
        "assert(data[2][\"name\"] == \"Carol\")\n"
    );
    /* Empty chain acts as equal */
    RUN_OK(
        "import \"cmp\"\n"
        "var fn = cmp.chain([])\n"
        "assert(fn(1, 2) == 0)\n"
    );
}

/* ---- null_last ---- */
static void test_null_last(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "var fn = cmp.null_last(cmp.compare)\n"
        "assert(fn(nil, nil)  == 0)\n"
        "assert(fn(nil, 1)    == 1)\n"
        "assert(fn(1,   nil)  == -1)\n"
        "assert(fn(1,   2)    == -1)\n"
        "assert(fn(2,   1)    == 1)\n"
    );
}

/* ---- null_first ---- */
static void test_null_first(void) {
    RUN_OK(
        "import \"cmp\"\n"
        "var fn = cmp.null_first(cmp.compare)\n"
        "assert(fn(nil, nil)  == 0)\n"
        "assert(fn(nil, 1)    == -1)\n"
        "assert(fn(1,   nil)  == 1)\n"
        "assert(fn(1,   2)    == -1)\n"
        "assert(fn(2,   1)    == 1)\n"
    );
}

int main(void) {
    test_compare();
    test_predicates();
    test_min_max();
    test_clamp();
    test_natural();
    test_reversed();
    test_by_key();
    test_chain();
    test_null_last();
    test_null_first();
    printf("test_stdlib_cmp: all passed\n");
    return 0;
}
