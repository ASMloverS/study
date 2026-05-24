/* tests/unit/test_stdlib_sort.c - STDLIB-20: sort module */
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
        fprintf(stderr, "FAIL test_stdlib_sort.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_sort.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- sort.sort (in-place) ---- */
static void test_sort_basic(void) {
    /* numeric ascending */
    RUN_OK(
        "import \"sort\"\n"
        "var nums = [3, 1, 4, 1, 5, 9, 2, 6]\n"
        "sort.sort(nums)\n"
        "assert(nums[0] == 1)\n"
        "assert(nums[1] == 1)\n"
        "assert(nums[2] == 2)\n"
        "assert(nums[7] == 9)\n"
    );
    /* empty list */
    RUN_OK(
        "import \"sort\"\n"
        "var e = []\n"
        "sort.sort(e)\n"
        "assert(len(e) == 0)\n"
    );
    /* single element */
    RUN_OK(
        "import \"sort\"\n"
        "var s = [42]\n"
        "sort.sort(s)\n"
        "assert(s[0] == 42)\n"
    );
    /* reverse=true */
    RUN_OK(
        "import \"sort\"\n"
        "var nums = [3, 1, 4, 1, 5]\n"
        "sort.sort(nums, nil, true)\n"
        "assert(nums[0] == 5)\n"
        "assert(nums[4] == 1)\n"
    );
    /* key function */
    RUN_OK(
        "import \"sort\"\n"
        "var words = [\"banana\", \"apple\", \"cherry\", \"fig\"]\n"
        "sort.sort(words, fun(w){ return len(w) })\n"
        "assert(words[0] == \"fig\")\n"
        "assert(words[3] == \"banana\" or words[3] == \"cherry\")\n"
    );
    /* type error */
    RUN_ERR("import \"sort\"\nsort.sort(42)\n");
    RUN_ERR("import \"sort\"\nsort.sort([1,2], 42)\n");
}

/* ---- sort.stable_sort ---- */
static void test_stable_sort(void) {
    /* stability: equal keys preserve original order */
    RUN_OK(
        "import \"sort\"\n"
        "var lst = [[1, \"b\"], [1, \"a\"], [2, \"c\"]]\n"
        "sort.stable_sort(lst, fun(x){ return x[0] })\n"
        "assert(lst[0][1] == \"b\")\n"
        "assert(lst[1][1] == \"a\")\n"
        "assert(lst[2][1] == \"c\")\n"
    );
    /* ascending strings */
    RUN_OK(
        "import \"sort\"\n"
        "var lst = [\"zebra\", \"apple\", \"mango\"]\n"
        "sort.stable_sort(lst)\n"
        "assert(lst[0] == \"apple\")\n"
        "assert(lst[1] == \"mango\")\n"
        "assert(lst[2] == \"zebra\")\n"
    );
}

/* ---- sort.sorted (copy) ---- */
static void test_sorted(void) {
    RUN_OK(
        "import \"sort\"\n"
        "var orig = [3, 1, 4, 1, 5]\n"
        "var copy = sort.sorted(orig)\n"
        /* original unchanged */
        "assert(orig[0] == 3)\n"
        "assert(copy[0] == 1)\n"
        "assert(copy[4] == 5)\n"
        "assert(len(copy) == 5)\n"
    );
    /* with key and reverse */
    RUN_OK(
        "import \"sort\"\n"
        "var words = [\"banana\", \"apple\", \"fig\"]\n"
        "var r = sort.sorted(words, fun(w){ return len(w) }, true)\n"
        "assert(r[0] == \"banana\")\n"
        "assert(r[2] == \"fig\")\n"
    );
    /* empty */
    RUN_OK(
        "import \"sort\"\n"
        "var r = sort.sorted([])\n"
        "assert(len(r) == 0)\n"
    );
    /* cmp argument */
    RUN_OK(
        "import \"sort\"\n"
        "var nums = [3, 1, 2]\n"
        "fun cmp(a, b) { if (a < b) { return -1 } if (a > b) { return 1 } return 0 }\n"
        "var r = sort.sorted(nums, nil, false, cmp)\n"
        "assert(r[0] == 1)\n"
        "assert(r[1] == 2)\n"
        "assert(r[2] == 3)\n"
    );
}

/* ---- sort.is_sorted / sort.is_sorted_desc ---- */
static void test_is_sorted(void) {
    RUN_OK(
        "import \"sort\"\n"
        "assert(sort.is_sorted([1, 2, 3, 4]) == true)\n"
        "assert(sort.is_sorted([1, 1, 2]) == true)\n"
        "assert(sort.is_sorted([3, 1, 2]) == false)\n"
        "assert(sort.is_sorted([]) == true)\n"
        "assert(sort.is_sorted([7]) == true)\n"
    );
    RUN_OK(
        "import \"sort\"\n"
        "assert(sort.is_sorted_desc([4, 3, 2, 1]) == true)\n"
        "assert(sort.is_sorted_desc([3, 3, 1]) == true)\n"
        "assert(sort.is_sorted_desc([1, 3, 2]) == false)\n"
        "assert(sort.is_sorted_desc([]) == true)\n"
    );
    /* with key function */
    RUN_OK(
        "import \"sort\"\n"
        "var words = [\"a\", \"bb\", \"ccc\"]\n"
        "assert(sort.is_sorted(words, fun(w){ return len(w) }) == true)\n"
        "assert(sort.is_sorted_desc(words, fun(w){ return len(w) }) == false)\n"
    );
    RUN_ERR("import \"sort\"\nsort.is_sorted(\"not a list\")\n");
}

/* ---- sort.binary_search ---- */
static void test_binary_search(void) {
    RUN_OK(
        "import \"sort\"\n"
        "var lst = [1, 2, 3, 4, 5]\n"
        "assert(sort.binary_search(lst, 3) == 2)\n"
        "assert(sort.binary_search(lst, 1) == 0)\n"
        "assert(sort.binary_search(lst, 5) == 4)\n"
        "assert(sort.binary_search(lst, 6) == -1)\n"
        "assert(sort.binary_search(lst, 0) == -1)\n"
    );
    /* empty list */
    RUN_OK(
        "import \"sort\"\n"
        "assert(sort.binary_search([], 1) == -1)\n"
    );
    /* duplicates: finds one of them */
    RUN_OK(
        "import \"sort\"\n"
        "var lst = [1, 3, 3, 5]\n"
        "var idx = sort.binary_search(lst, 3)\n"
        "assert(idx == 1 or idx == 2)\n"
    );
    /* with key function */
    RUN_OK(
        "import \"sort\"\n"
        "var words = [\"a\", \"bb\", \"ccc\", \"dddd\"]\n"
        "var idx = sort.binary_search(words, 3, fun(w){ return len(w) })\n"
        "assert(idx == 2)\n"
    );
    RUN_ERR("import \"sort\"\nsort.binary_search(42, 1)\n");
}

/* ---- sort.bisect_left / sort.bisect_right ---- */
static void test_bisect(void) {
    RUN_OK(
        "import \"sort\"\n"
        "var lst = [1, 3, 3, 5]\n"
        "assert(sort.bisect_left(lst, 3)  == 1)\n"
        "assert(sort.bisect_right(lst, 3) == 3)\n"
        "assert(sort.bisect_left(lst, 0)  == 0)\n"
        "assert(sort.bisect_right(lst, 0) == 0)\n"
        "assert(sort.bisect_left(lst, 6)  == 4)\n"
        "assert(sort.bisect_right(lst, 6) == 4)\n"
    );
    /* single element */
    RUN_OK(
        "import \"sort\"\n"
        "var lst = [5]\n"
        "assert(sort.bisect_left(lst, 5)  == 0)\n"
        "assert(sort.bisect_right(lst, 5) == 1)\n"
        "assert(sort.bisect_left(lst, 4)  == 0)\n"
        "assert(sort.bisect_left(lst, 6)  == 1)\n"
    );
    /* empty */
    RUN_OK(
        "import \"sort\"\n"
        "assert(sort.bisect_left([], 3)  == 0)\n"
        "assert(sort.bisect_right([], 3) == 0)\n"
    );
    RUN_ERR("import \"sort\"\nsort.bisect_left(\"x\", 1)\n");
    RUN_ERR("import \"sort\"\nsort.bisect_right(\"x\", 1)\n");
}

/* ---- sort.search_range ---- */
static void test_search_range(void) {
    RUN_OK(
        "import \"sort\"\n"
        "var lst = [1, 3, 3, 3, 5]\n"
        "var r = sort.search_range(lst, 3)\n"
        "assert(r[0] == 1)\n"
        "assert(r[1] == 4)\n"
    );
    /* not found */
    RUN_OK(
        "import \"sort\"\n"
        "var r = sort.search_range([1, 2, 4, 5], 3)\n"
        "assert(r[0] == r[1])\n"
    );
    /* empty list */
    RUN_OK(
        "import \"sort\"\n"
        "var r = sort.search_range([], 1)\n"
        "assert(r[0] == 0)\n"
        "assert(r[1] == 0)\n"
    );
    RUN_ERR("import \"sort\"\nsort.search_range(42, 1)\n");
}

/* ---- large list stress test ---- */
static void test_large(void) {
    /* Build a reverse-sorted list of 200 elements, sort it, verify */
    RUN_OK(
        "import \"sort\"\n"
        "var lst = []\n"
        "var i = 200\n"
        "while (i > 0) { lst.push(i)  i = i - 1 }\n"
        "sort.sort(lst)\n"
        "assert(lst[0] == 1)\n"
        "assert(lst[199] == 200)\n"
        "assert(sort.is_sorted(lst) == true)\n"
    );
}

int main(void) {
    test_sort_basic();
    test_stable_sort();
    test_sorted();
    test_is_sorted();
    test_binary_search();
    test_bisect();
    test_search_range();
    test_large();
    printf("test_stdlib_sort: all passed\n");
    return 0;
}
