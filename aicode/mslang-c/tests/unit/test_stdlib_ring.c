/* tests/unit/test_stdlib_ring.c - STDLIB-38: ring module */
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
        fprintf(stderr, "FAIL test_stdlib_ring.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

/* ---- new + push + to_list ---- */
static void test_new_push_to_list(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(3)\n"
        "r.push(1)\n"
        "r.push(2)\n"
        "r.push(3)\n"
        "var lst = r.to_list()\n"
        "assert(lst[0] == 1)\n"
        "assert(lst[1] == 2)\n"
        "assert(lst[2] == 3)\n"
        "assert(r.size() == 3)\n"
    );
}

/* ---- capacity / size / is_full / is_empty ---- */
static void test_capacity_state(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(4)\n"
        "assert(r.capacity() == 4)\n"
        "assert(r.size() == 0)\n"
        "assert(r.is_empty() == true)\n"
        "assert(r.is_full() == false)\n"
        "r.push(1)\n"
        "r.push(2)\n"
        "r.push(3)\n"
        "r.push(4)\n"
        "assert(r.is_full() == true)\n"
        "assert(r.is_empty() == false)\n"
        "assert(r.size() == 4)\n"
    );
}

/* ---- push when full evicts oldest ---- */
static void test_push_full_evicts(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(3)\n"
        "assert(r.push(1) == nil)\n"
        "assert(r.push(2) == nil)\n"
        "assert(r.push(3) == nil)\n"
        "assert(r.is_full() == true)\n"
        "var evicted = r.push(4)\n"
        "assert(evicted == 1)\n"
        "var lst = r.to_list()\n"
        "assert(lst[0] == 2)\n"
        "assert(lst[1] == 3)\n"
        "assert(lst[2] == 4)\n"
    );
}

/* ---- spec example ---- */
static void test_spec_example(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(3)\n"
        "r.push(1)\n"
        "r.push(2)\n"
        "r.push(3)\n"
        "assert(r.is_full() == true)\n"
        "var evicted = r.push(4)\n"
        "assert(evicted == 1)\n"
        "var lst = r.to_list()\n"
        "assert(lst[0] == 2)\n"
        "assert(lst[1] == 3)\n"
        "assert(lst[2] == 4)\n"
    );
}

/* ---- pop ---- */
static void test_pop(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(3)\n"
        "r.push(10)\n"
        "r.push(20)\n"
        "r.push(30)\n"
        "assert(r.pop() == 10)\n"
        "assert(r.pop() == 20)\n"
        "assert(r.size() == 1)\n"
        "assert(r.pop() == 30)\n"
        "assert(r.is_empty() == true)\n"
        "assert(r.pop() == nil)\n"
    );
}

/* ---- peek ---- */
static void test_peek(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(3)\n"
        "assert(r.peek() == nil)\n"
        "r.push(5)\n"
        "r.push(6)\n"
        "assert(r.peek() == 5)\n"
        "assert(r.size() == 2)\n"
    );
}

/* ---- clear ---- */
static void test_clear(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(3)\n"
        "r.push(1)\n"
        "r.push(2)\n"
        "r.clear()\n"
        "assert(r.size() == 0)\n"
        "assert(r.is_empty() == true)\n"
        "assert(r.capacity() == 3)\n"
    );
}

/* ---- push after pop cycles correctly ---- */
static void test_wrap_around(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(3)\n"
        "r.push(1)\n"
        "r.push(2)\n"
        "r.push(3)\n"
        "r.pop()\n"
        "r.pop()\n"
        "r.push(4)\n"
        "r.push(5)\n"
        "var lst = r.to_list()\n"
        "assert(lst[0] == 3)\n"
        "assert(lst[1] == 4)\n"
        "assert(lst[2] == 5)\n"
    );
}

/* ---- capacity 1 edge case ---- */
static void test_capacity_one(void) {
    RUN_OK(
        "import \"ring\"\n"
        "var r = ring.new(1)\n"
        "assert(r.push(42) == nil)\n"
        "assert(r.is_full() == true)\n"
        "assert(r.push(99) == 42)\n"
        "assert(r.peek() == 99)\n"
        "assert(r.size() == 1)\n"
    );
}

int main(void) {
    test_new_push_to_list();
    test_capacity_state();
    test_push_full_evicts();
    test_spec_example();
    test_pop();
    test_peek();
    test_clear();
    test_wrap_around();
    test_capacity_one();
    printf("test_stdlib_ring: all passed\n");
    return 0;
}
