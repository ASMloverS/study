/* tests/unit/test_stdlib_heap.c - STDLIB-22: heap module */
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
        fprintf(stderr, "FAIL test_stdlib_heap.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_heap.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- construction ---- */
static void test_construction(void) {
    /* empty min-heap */
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.new()\n"
        "assert(h.size() == 0)\n"
        "assert(h.is_empty() == true)\n"
    );
    /* heap.heapify from list */
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.heapify([5, 2, 8, 1, 9])\n"
        "assert(h.size() == 5)\n"
        "assert(h.pop() == 1)\n"
    );
    /* heapify empty list */
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.heapify([])\n"
        "assert(h.is_empty() == true)\n"
    );
}

/* ---- min-heap push/pop order ---- */
static void test_min_heap(void) {
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.new()\n"
        "h.push(5)\n"
        "h.push(1)\n"
        "h.push(3)\n"
        "assert(h.size() == 3)\n"
        "assert(h.pop() == 1)\n"
        "assert(h.pop() == 3)\n"
        "assert(h.pop() == 5)\n"
        "assert(h.is_empty() == true)\n"
    );
    /* duplicate values */
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.new()\n"
        "h.push(2)\n"
        "h.push(2)\n"
        "h.push(1)\n"
        "assert(h.pop() == 1)\n"
        "assert(h.pop() == 2)\n"
        "assert(h.pop() == 2)\n"
    );
}

/* ---- max-heap (reversed comparator) ---- */
static void test_max_heap(void) {
    RUN_OK(
        "import \"heap\"\n"
        "fun max_cmp(a,b) { if (b < a) { return -1 } if (b > a) { return 1 } return 0 }\n"
        "var h = heap.new(max_cmp)\n"
        "h.push(5)\n"
        "h.push(1)\n"
        "h.push(9)\n"
        "h.push(3)\n"
        "assert(h.pop() == 9)\n"
        "assert(h.pop() == 5)\n"
        "assert(h.pop() == 3)\n"
        "assert(h.pop() == 1)\n"
    );
}

/* ---- peek ---- */
static void test_peek(void) {
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.new()\n"
        "h.push(10)\n"
        "h.push(2)\n"
        "h.push(7)\n"
        "assert(h.peek() == 2)\n"
        "assert(h.size() == 3)\n"  /* peek does not remove */
        "assert(h.pop() == 2)\n"
        "assert(h.peek() == 7)\n"
    );
    /* peek on empty heap */
    RUN_ERR("import \"heap\"\nheap.new().peek()\n");
}

/* ---- pop on empty heap ---- */
static void test_pop_empty(void) {
    RUN_ERR("import \"heap\"\nheap.new().pop()\n");
}

/* ---- push_pop ---- */
static void test_push_pop(void) {
    /* push_pop: value beats current top → return value immediately */
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.new()\n"
        "h.push(5)\n"
        "h.push(8)\n"
        "var r = h.push_pop(1)\n"
        "assert(r == 1)\n"      /* 1 < 5, returned without entering heap */
        "assert(h.size() == 2)\n"
    );
    /* push_pop: value loses to current top → return current top */
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.new()\n"
        "h.push(3)\n"
        "h.push(7)\n"
        "var r = h.push_pop(10)\n"
        "assert(r == 3)\n"      /* 3 was the min; 10 is inserted in its place */
        "assert(h.size() == 2)\n"
    );
}

/* ---- to_list / to_sorted_list ---- */
static void test_list_conversions(void) {
    /* to_list: returns heap-order copy, does not drain */
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.new()\n"
        "h.push(3)\n"
        "h.push(1)\n"
        "h.push(2)\n"
        "var lst = h.to_list()\n"
        "assert(len(lst) == 3)\n"
        "assert(h.size() == 3)\n"  /* heap is not drained */
    );
    /* to_sorted_list: drains heap in priority order */
    RUN_OK(
        "import \"heap\"\n"
        "var h = heap.new()\n"
        "h.push(4)\n"
        "h.push(2)\n"
        "h.push(9)\n"
        "h.push(1)\n"
        "var s = h.to_sorted_list()\n"
        "assert(len(s) == 4)\n"
        "assert(s[0] == 1)\n"
        "assert(s[1] == 2)\n"
        "assert(s[2] == 4)\n"
        "assert(s[3] == 9)\n"
        "assert(h.is_empty() == true)\n"
    );
}

/* ---- heap.nsmallest / heap.nlargest ---- */
static void test_nsmallest_nlargest(void) {
    RUN_OK(
        "import \"heap\"\n"
        "var s = heap.nsmallest(3, [5, 2, 8, 1, 9, 3])\n"
        "assert(len(s) == 3)\n"
        "assert(s[0] == 1)\n"
        "assert(s[1] == 2)\n"
        "assert(s[2] == 3)\n"
    );
    RUN_OK(
        "import \"heap\"\n"
        "var l = heap.nlargest(3, [5, 2, 8, 1, 9, 3])\n"
        "assert(len(l) == 3)\n"
        "assert(l[0] == 9)\n"
        "assert(l[1] == 8)\n"
        "assert(l[2] == 5)\n"
    );
    /* k > list size: clamps to list size */
    RUN_OK(
        "import \"heap\"\n"
        "var s = heap.nsmallest(10, [3, 1, 2])\n"
        "assert(len(s) == 3)\n"
    );
    /* k == 0: empty result */
    RUN_OK(
        "import \"heap\"\n"
        "var s = heap.nsmallest(0, [1, 2, 3])\n"
        "assert(len(s) == 0)\n"
    );
}

/* ---- heapify with custom comparator ---- */
static void test_heapify_custom_cmp(void) {
    RUN_OK(
        "import \"heap\"\n"
        "fun max_cmp(a,b) { if (b < a) { return -1 } if (b > a) { return 1 } return 0 }\n"
        "var h = heap.heapify([3, 1, 5, 2], max_cmp)\n"
        "assert(h.pop() == 5)\n"
        "assert(h.pop() == 3)\n"
    );
}

/* ---- error cases ---- */
static void test_errors(void) {
    /* heap.new with non-callable arg */
    RUN_ERR("import \"heap\"\nheap.new(42)\n");
    /* heap.heapify with non-list */
    RUN_ERR("import \"heap\"\nheap.heapify(99)\n");
    /* heap.nsmallest bad args */
    RUN_ERR("import \"heap\"\nheap.nsmallest(\"bad\", [1,2])\n");
    RUN_ERR("import \"heap\"\nheap.nlargest(2, \"bad\")\n");
}

int main(void) {
    test_construction();
    test_min_heap();
    test_max_heap();
    test_peek();
    test_pop_empty();
    test_push_pop();
    test_list_conversions();
    test_nsmallest_nlargest();
    test_heapify_custom_cmp();
    test_errors();
    printf("test_stdlib_heap: all passed\n");
    return 0;
}
