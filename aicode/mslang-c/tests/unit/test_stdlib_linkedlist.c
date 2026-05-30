/* tests/unit/test_stdlib_linkedlist.c - STDLIB-37: linkedlist module */
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
        fprintf(stderr, "FAIL test_stdlib_linkedlist.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

/* ---- new + push_back + to_list ---- */
static void test_new_push_back(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "l.push_back(1)\n"
        "l.push_back(2)\n"
        "l.push_back(3)\n"
        "var lst = l.to_list()\n"
        "assert(lst[0] == 1)\n"
        "assert(lst[1] == 2)\n"
        "assert(lst[2] == 3)\n"
        "assert(l.size() == 3)\n"
    );
}

/* ---- push_front ---- */
static void test_push_front(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "l.push_back(2)\n"
        "l.push_back(3)\n"
        "l.push_front(1)\n"
        "var lst = l.to_list()\n"
        "assert(lst[0] == 1)\n"
        "assert(lst[1] == 2)\n"
        "assert(lst[2] == 3)\n"
    );
}

/* ---- pop_front ---- */
static void test_pop_front(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "l.push_back(10)\n"
        "l.push_back(20)\n"
        "l.push_back(30)\n"
        "assert(l.pop_front() == 10)\n"
        "assert(l.pop_front() == 20)\n"
        "assert(l.size() == 1)\n"
    );
}

/* ---- pop_back ---- */
static void test_pop_back(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "l.push_back(10)\n"
        "l.push_back(20)\n"
        "assert(l.pop_back() == 20)\n"
        "assert(l.size() == 1)\n"
        "assert(l.pop_back() == 10)\n"
        "assert(l.is_empty() == true)\n"
    );
}

/* ---- peek_front / peek_back ---- */
static void test_peek(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "l.push_back(5)\n"
        "l.push_back(6)\n"
        "assert(l.peek_front() == 5)\n"
        "assert(l.peek_back()  == 6)\n"
        "assert(l.size() == 2)\n"
    );
}

/* ---- insert_after ---- */
static void test_insert_after(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "var n1 = l.push_back(1)\n"
        "var n3 = l.push_back(3)\n"
        "l.insert_after(n1, 2)\n"
        "var lst = l.to_list()\n"
        "assert(lst[0] == 1)\n"
        "assert(lst[1] == 2)\n"
        "assert(lst[2] == 3)\n"
    );
}

/* ---- insert_before ---- */
static void test_insert_before(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "var n1 = l.push_back(1)\n"
        "var n3 = l.push_back(3)\n"
        "l.insert_before(n3, 2)\n"
        "var lst = l.to_list()\n"
        "assert(lst[0] == 1)\n"
        "assert(lst[1] == 2)\n"
        "assert(lst[2] == 3)\n"
    );
}

/* ---- remove ---- */
static void test_remove(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "var n1 = l.push_back(1)\n"
        "var n2 = l.push_back(2)\n"
        "var n3 = l.push_back(3)\n"
        "var v = l.remove(n2)\n"
        "assert(v == 2)\n"
        "assert(l.size() == 2)\n"
        "var lst = l.to_list()\n"
        "assert(lst[0] == 1)\n"
        "assert(lst[1] == 3)\n"
    );
}

/* ---- remove head ---- */
static void test_remove_head(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "var n1 = l.push_back(1)\n"
        "var n2 = l.push_back(2)\n"
        "l.remove(n1)\n"
        "assert(l.size() == 1)\n"
        "assert(l.peek_front() == 2)\n"
    );
}

/* ---- remove tail ---- */
static void test_remove_tail(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "var n1 = l.push_back(1)\n"
        "var n2 = l.push_back(2)\n"
        "l.remove(n2)\n"
        "assert(l.size() == 1)\n"
        "assert(l.peek_back() == 1)\n"
    );
}

/* ---- size / is_empty ---- */
static void test_size_empty(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "assert(l.is_empty() == true)\n"
        "assert(l.size() == 0)\n"
        "l.push_back(1)\n"
        "assert(l.is_empty() == false)\n"
        "assert(l.size() == 1)\n"
    );
}

/* ---- each ---- */
static void test_each(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.from_list([1, 2, 3])\n"
        "var sum = 0\n"
        "l.each(fun(v){ sum = sum + v })\n"
        "assert(sum == 6)\n"
    );
}

/* ---- from_list ---- */
static void test_from_list(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.from_list([7, 8, 9])\n"
        "assert(l.size() == 3)\n"
        "var lst = l.to_list()\n"
        "assert(lst[0] == 7)\n"
        "assert(lst[1] == 8)\n"
        "assert(lst[2] == 9)\n"
    );
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.from_list([])\n"
        "assert(l.is_empty() == true)\n"
    );
}

/* ---- spec example ---- */
static void test_spec_example(void) {
    RUN_OK(
        "import \"linkedlist\"\n"
        "var l = linkedlist.new()\n"
        "var n1 = l.push_back(1)\n"
        "var n2 = l.push_back(3)\n"
        "l.insert_before(n2, 2)\n"
        "var sum = 0\n"
        "l.each(fun(v){ sum = sum + v })\n"
        "assert(sum == 6)\n"
        "l.remove(n1)\n"
        "var lst = l.to_list()\n"
        "assert(lst[0] == 2)\n"
        "assert(lst[1] == 3)\n"
    );
}

int main(void) {
    test_new_push_back();
    test_push_front();
    test_pop_front();
    test_pop_back();
    test_peek();
    test_insert_after();
    test_insert_before();
    test_remove();
    test_remove_head();
    test_remove_tail();
    test_size_empty();
    test_each();
    test_from_list();
    test_spec_example();
    printf("test_stdlib_linkedlist: all passed\n");
    return 0;
}
