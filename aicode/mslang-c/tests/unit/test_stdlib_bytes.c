/* tests/unit/test_stdlib_bytes.c - STDLIB-27: bytes module */
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
        fprintf(stderr, "FAIL test_stdlib_bytes.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_bytes.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- bytes.new ---- */
static void test_new(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.new()\n"
        "assert(b.len() == 0)\n"
    );
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.new(4, 0xAB)\n"
        "assert(b.len() == 4)\n"
        "assert(b.get(0) == 0xAB)\n"
        "assert(b.get(3) == 0xAB)\n"
    );
}

/* ---- bytes.from_str ---- */
static void test_from_str(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"Hello\")\n"
        "assert(b.len() == 5)\n"
        "assert(b.get(0) == 72)\n"
    );
    RUN_ERR(
        "import \"bytes\"\n"
        "bytes.from_str(\"Hello\", \"utf16\")\n"
    );
}

/* ---- bytes.from_list ---- */
static void test_from_list(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_list([72, 101, 108, 108, 111])\n"
        "assert(b.len() == 5)\n"
        "assert(b.get(0) == 72)\n"
        "assert(b.get(4) == 111)\n"
    );
    RUN_ERR(
        "import \"bytes\"\n"
        "bytes.from_list([256])\n"
    );
    RUN_ERR(
        "import \"bytes\"\n"
        "bytes.from_list([-1])\n"
    );
}

/* ---- bytes.copy ---- */
static void test_copy(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var a = bytes.from_str(\"hi\")\n"
        "var b = bytes.copy(a)\n"
        "b.set(0, 88)\n"
        "assert(a.get(0) == 104)\n"
        "assert(b.get(0) == 88)\n"
    );
}

/* ---- bytes.concat ---- */
static void test_concat(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var a = bytes.from_str(\"foo\")\n"
        "var b = bytes.from_str(\"bar\")\n"
        "var c = bytes.concat(a, b)\n"
        "assert(c.len() == 6)\n"
        "assert(bytes.to_str(c) == \"foobar\")\n"
    );
    /* three args */
    RUN_OK(
        "import \"bytes\"\n"
        "var a = bytes.from_str(\"a\")\n"
        "var b = bytes.from_str(\"b\")\n"
        "var c = bytes.from_str(\"c\")\n"
        "var d = bytes.concat(a, b, c)\n"
        "assert(bytes.to_str(d) == \"abc\")\n"
    );
}

/* ---- bytes.repeat ---- */
static void test_repeat(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"ab\")\n"
        "var r = bytes.repeat(b, 3)\n"
        "assert(bytes.to_str(r) == \"ababab\")\n"
    );
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"x\")\n"
        "var r = bytes.repeat(b, 0)\n"
        "assert(r.len() == 0)\n"
    );
}

/* ---- bytes.to_str ---- */
static void test_to_str(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"Hello\")\n"
        "assert(bytes.to_str(b) == \"Hello\")\n"
    );
}

/* ---- bytes.to_list ---- */
static void test_to_list(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"Hi\")\n"
        "var lst = bytes.to_list(b)\n"
        "assert(lst[0] == 72)\n"
        "assert(lst[1] == 105)\n"
        "assert(lst.len() == 2)\n"
    );
}

/* ---- bytes.equal ---- */
static void test_equal(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var a = bytes.from_str(\"Hello\")\n"
        "var b = bytes.from_str(\"Hello\")\n"
        "var c = bytes.from_str(\"World\")\n"
        "assert(bytes.equal(a, b) == true)\n"
        "assert(bytes.equal(a, c) == false)\n"
    );
}

/* ---- bytes.compare ---- */
static void test_compare(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var a = bytes.from_str(\"abc\")\n"
        "var b = bytes.from_str(\"abc\")\n"
        "var c = bytes.from_str(\"abd\")\n"
        "var d = bytes.from_str(\"abb\")\n"
        "assert(bytes.compare(a, b) == 0)\n"
        "assert(bytes.compare(a, c) == -1)\n"
        "assert(bytes.compare(a, d) == 1)\n"
    );
}

/* ---- bytes.contains ---- */
static void test_contains(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var h = bytes.from_str(\"Hello World\")\n"
        "var n = bytes.from_str(\"World\")\n"
        "var m = bytes.from_str(\"xyz\")\n"
        "assert(bytes.contains(h, n) == true)\n"
        "assert(bytes.contains(h, m) == false)\n"
    );
}

/* ---- bytes.index ---- */
static void test_index(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var h = bytes.from_str(\"Hello World\")\n"
        "var n = bytes.from_str(\"World\")\n"
        "var m = bytes.from_str(\"xyz\")\n"
        "assert(bytes.index(h, n) == 6)\n"
        "assert(bytes.index(h, m) == -1)\n"
    );
}

/* ---- bytes.count ---- */
static void test_count(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"ababab\")\n"
        "var sub = bytes.from_str(\"ab\")\n"
        "assert(bytes.count(b, sub) == 3)\n"
    );
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"aaa\")\n"
        "var sub = bytes.from_str(\"aa\")\n"
        "assert(bytes.count(b, sub) == 1)\n"
    );
}

/* ---- bytes.slice ---- */
static void test_slice(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"Hello World\")\n"
        "var s = bytes.slice(b, 0, 5)\n"
        "assert(bytes.to_str(s) == \"Hello\")\n"
    );
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"Hello\")\n"
        "var s = bytes.slice(b, 2, -1)\n"
        "assert(bytes.to_str(s) == \"llo\")\n"
    );
}

/* ---- bytes.trim ---- */
static void test_trim(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b   = bytes.from_str(\"  hello  \")\n"
        "var sp  = bytes.from_list([32])\n"
        "var t   = bytes.trim(b, sp)\n"
        "assert(bytes.to_str(t) == \"hello\")\n"
    );
}

/* ---- bytes.replace ---- */
static void test_replace(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b   = bytes.from_str(\"ababab\")\n"
        "var old = bytes.from_str(\"ab\")\n"
        "var nw  = bytes.from_str(\"X\")\n"
        "var r   = bytes.replace(b, old, nw)\n"
        "assert(bytes.to_str(r) == \"XXX\")\n"
    );
}

/* ---- bytes.reverse ---- */
static void test_reverse(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b = bytes.from_str(\"Hello\")\n"
        "var r = bytes.reverse(b)\n"
        "assert(bytes.to_str(r) == \"olleH\")\n"
        "assert(bytes.to_str(b) == \"Hello\")\n"
    );
}

/* ---- spec example ---- */
static void test_spec_example(void) {
    RUN_OK(
        "import \"bytes\"\n"
        "var b1 = bytes.from_str(\"Hello\")\n"
        "var b2 = bytes.from_str(\" World\")\n"
        "var joined = bytes.concat(b1, b2)\n"
        "assert(bytes.to_str(joined) == \"Hello World\")\n"
        "assert(bytes.equal(b1, bytes.from_str(\"Hello\")) == true)\n"
        "assert(bytes.index(joined, bytes.from_str(\"World\")) == 6)\n"
    );
    RUN_OK(
        "import \"bytes\"\n"
        "var nums = bytes.from_list([72, 101, 108, 108, 111])\n"
        "assert(bytes.to_str(nums) == \"Hello\")\n"
    );
    RUN_OK(
        "import \"bytes\"\n"
        "var repeated = bytes.repeat(bytes.from_str(\"ab\"), 3)\n"
        "assert(bytes.to_str(repeated) == \"ababab\")\n"
    );
}

int main(void) {
    test_new();
    test_from_str();
    test_from_list();
    test_copy();
    test_concat();
    test_repeat();
    test_to_str();
    test_to_list();
    test_equal();
    test_compare();
    test_contains();
    test_index();
    test_count();
    test_slice();
    test_trim();
    test_replace();
    test_reverse();
    test_spec_example();
    printf("test_stdlib_bytes: all passed\n");
    return 0;
}
