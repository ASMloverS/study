/* tests/unit/test_stdlib_deque.c - STDLIB-36: deque module */
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
        fprintf(stderr, "FAIL test_stdlib_deque.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

/* ---- new + push_back + to_list ---- */
static void test_new_push_back(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.new()\n"
        "d.push_back(1)\n"
        "d.push_back(2)\n"
        "d.push_back(3)\n"
        "var lst = d.to_list()\n"
        "assert(lst[0] == 1)\n"
        "assert(lst[1] == 2)\n"
        "assert(lst[2] == 3)\n"
        "assert(d.size() == 3)\n"
    );
}

/* ---- push_front ---- */
static void test_push_front(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.new()\n"
        "d.push_back(1)\n"
        "d.push_back(2)\n"
        "d.push_front(0)\n"
        "var lst = d.to_list()\n"
        "assert(lst[0] == 0)\n"
        "assert(lst[1] == 1)\n"
        "assert(lst[2] == 2)\n"
    );
}

/* ---- pop_back ---- */
static void test_pop_back(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.new()\n"
        "d.push_back(10)\n"
        "d.push_back(20)\n"
        "assert(d.pop_back() == 20)\n"
        "assert(d.size() == 1)\n"
        "assert(d.pop_back() == 10)\n"
        "assert(d.is_empty() == true)\n"
    );
}

/* ---- pop_front ---- */
static void test_pop_front(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.new()\n"
        "d.push_back(10)\n"
        "d.push_back(20)\n"
        "d.push_back(30)\n"
        "assert(d.pop_front() == 10)\n"
        "assert(d.pop_front() == 20)\n"
        "assert(d.size() == 1)\n"
    );
}

/* ---- peek_front / peek_back ---- */
static void test_peek(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.new()\n"
        "d.push_back(5)\n"
        "d.push_back(6)\n"
        "assert(d.peek_front() == 5)\n"
        "assert(d.peek_back()  == 6)\n"
        "assert(d.size() == 2)\n"  /* peek does not remove */
    );
}

/* ---- get ---- */
static void test_get(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.from_list([100, 200, 300])\n"
        "assert(d.get(0) == 100)\n"
        "assert(d.get(1) == 200)\n"
        "assert(d.get(2) == 300)\n"
    );
}

/* ---- size / is_empty ---- */
static void test_size_empty(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.new()\n"
        "assert(d.is_empty() == true)\n"
        "assert(d.size() == 0)\n"
        "d.push_back(1)\n"
        "assert(d.is_empty() == false)\n"
        "assert(d.size() == 1)\n"
    );
}

/* ---- clear ---- */
static void test_clear(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.from_list([1, 2, 3])\n"
        "assert(d.size() == 3)\n"
        "d.clear()\n"
        "assert(d.is_empty() == true)\n"
        "assert(d.size() == 0)\n"
    );
}

/* ---- from_list ---- */
static void test_from_list(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.from_list([7, 8, 9])\n"
        "assert(d.size() == 3)\n"
        "var lst = d.to_list()\n"
        "assert(lst[0] == 7)\n"
        "assert(lst[1] == 8)\n"
        "assert(lst[2] == 9)\n"
    );
    /* empty list */
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.from_list([])\n"
        "assert(d.is_empty() == true)\n"
    );
}

/* ---- mixed push_front + push_back ordering ---- */
static void test_mixed_order(void) {
    RUN_OK(
        "import \"deque\"\n"
        "var d = deque.new()\n"
        "d.push_back(2)\n"
        "d.push_front(1)\n"
        "d.push_back(3)\n"
        "d.push_front(0)\n"
        "var lst = d.to_list()\n"
        "assert(lst[0] == 0)\n"
        "assert(lst[1] == 1)\n"
        "assert(lst[2] == 2)\n"
        "assert(lst[3] == 3)\n"
    );
}

int main(void) {
    test_new_push_back();
    test_push_front();
    test_pop_back();
    test_pop_front();
    test_peek();
    test_get();
    test_size_empty();
    test_clear();
    test_from_list();
    test_mixed_order();
    printf("test_stdlib_deque: all passed\n");
    return 0;
}
