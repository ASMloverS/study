/* tests/unit/test_stdlib_slices.c - STDLIB-18: slices module */
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
        fprintf(stderr, "FAIL test_stdlib_slices.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_slices.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- map ---- */
static void test_map(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.map([1,2,3], fun(x){ return x*2 })\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 2)\n"
        "assert(r[1] == 4)\n"
        "assert(r[2] == 6)\n"
    );
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.map([], fun(x){ return x })\n"
        "assert(len(r) == 0)\n"
    );
    RUN_ERR("import \"slices\"\nslices.map(42, fun(x){ return x })\n");
}

/* ---- filter ---- */
static void test_filter(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.filter([1,2,3,4,5], fun(x){ return x > 3 })\n"
        "assert(len(r) == 2)\n"
        "assert(r[0] == 4)\n"
        "assert(r[1] == 5)\n"
    );
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.filter([1,2,3], fun(x){ return false })\n"
        "assert(len(r) == 0)\n"
    );
}

/* ---- flat_map ---- */
static void test_flat_map(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.flat_map([1,2,3], fun(x){ return [x, x*10] })\n"
        "assert(len(r) == 6)\n"
        "assert(r[0] == 1)\n"
        "assert(r[1] == 10)\n"
    );
}

/* ---- reduce ---- */
static void test_reduce(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.reduce([1,2,3,4], fun(a,x){ return a+x }, 0)\n"
        "assert(r == 10)\n"
    );
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.reduce([], fun(a,x){ return a+x }, 99)\n"
        "assert(r == 99)\n"
    );
}

/* ---- any / all ---- */
static void test_any_all(void) {
    RUN_OK(
        "import \"slices\"\n"
        "assert(slices.any([1,2,3], fun(x){ return x > 2 }) == true)\n"
        "assert(slices.any([1,2,3], fun(x){ return x > 5 }) == false)\n"
        "assert(slices.all([1,2,3], fun(x){ return x > 0 }) == true)\n"
        "assert(slices.all([1,2,3], fun(x){ return x > 1 }) == false)\n"
    );
}

/* ---- count ---- */
static void test_count(void) {
    RUN_OK(
        "import \"slices\"\n"
        "assert(slices.count([1,2,3,4,5], fun(x){ return x % 2 == 0 }) == 2)\n"
    );
}

/* ---- group_by ---- */
static void test_group_by(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var g = slices.group_by([1,2,3,4], fun(x){ return x % 2 })\n"
        "assert(len(g[0]) == 2)\n"
        "assert(len(g[1]) == 2)\n"
    );
}

/* ---- partition ---- */
static void test_partition(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var p = slices.partition([1,2,3,4,5], fun(x){ return x % 2 == 0 })\n"
        "assert(len(p[0]) == 2)\n"
        "assert(len(p[1]) == 3)\n"
    );
}

/* ---- find / find_index ---- */
static void test_find(void) {
    RUN_OK(
        "import \"slices\"\n"
        "assert(slices.find([10,20,30], fun(x){ return x > 15 }) == 20)\n"
        "assert(slices.find([1,2,3], fun(x){ return x > 99 }) == nil)\n"
        "assert(slices.find_index([10,20,30], fun(x){ return x > 15 }) == 1)\n"
        "assert(slices.find_index([1,2,3], fun(x){ return x > 99 }) == -1)\n"
    );
}

/* ---- index_of / contains / last_index_of ---- */
static void test_index(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var lst = [3,1,4,1,5]\n"
        "assert(slices.index_of(lst, 1) == 1)\n"
        "assert(slices.index_of(lst, 9) == -1)\n"
        "assert(slices.contains(lst, 4) == true)\n"
        "assert(slices.contains(lst, 9) == false)\n"
        "assert(slices.last_index_of(lst, 1) == 3)\n"
        "assert(slices.last_index_of(lst, 9) == -1)\n"
    );
}

/* ---- reverse ---- */
static void test_reverse(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.reverse([1,2,3])\n"
        "assert(r[0] == 3)\n"
        "assert(r[1] == 2)\n"
        "assert(r[2] == 1)\n"
    );
    /* Does not mutate original */
    RUN_OK(
        "import \"slices\"\n"
        "var orig = [1,2,3]\n"
        "slices.reverse(orig)\n"
        "assert(orig[0] == 1)\n"
    );
}

/* ---- reverse_in_place ---- */
static void test_reverse_in_place(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var lst = [1,2,3]\n"
        "slices.reverse_in_place(lst)\n"
        "assert(lst[0] == 3)\n"
        "assert(lst[2] == 1)\n"
    );
}

/* ---- flatten ---- */
static void test_flatten(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.flatten([[1,2],[3],[4,5]])\n"
        "assert(len(r) == 5)\n"
        "assert(r[0] == 1)\n"
        "assert(r[4] == 5)\n"
    );
}

/* ---- unique ---- */
static void test_unique(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.unique([3,1,4,1,5,9,2,6,5])\n"
        "assert(len(r) == 7)\n"
        "assert(r[0] == 3)\n"
        "assert(r[1] == 1)\n"
    );
}

/* ---- chunk ---- */
static void test_chunk(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.chunk([1,2,3,4,5], 2)\n"
        "assert(len(r) == 3)\n"
        "assert(len(r[0]) == 2)\n"
        "assert(len(r[2]) == 1)\n"
    );
    RUN_ERR("import \"slices\"\nslices.chunk([1,2,3], 0)\n");
}

/* ---- zip ---- */
static void test_zip(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.zip([1,2,3], [\"a\",\"b\",\"c\"])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0][0] == 1)\n"
        "assert(r[0][1] == \"a\")\n"
    );
    /* truncates to shortest */
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.zip([1,2,3], [\"a\",\"b\"])\n"
        "assert(len(r) == 2)\n"
    );
}

/* ---- enumerate ---- */
static void test_enumerate(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.enumerate([\"a\",\"b\",\"c\"])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0][0] == 0)\n"
        "assert(r[0][1] == \"a\")\n"
        "assert(r[2][0] == 2)\n"
    );
}

/* ---- concat ---- */
static void test_concat(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.concat([1,2], [3,4], [5])\n"
        "assert(len(r) == 5)\n"
        "assert(r[0] == 1)\n"
        "assert(r[4] == 5)\n"
    );
}

/* ---- take / drop ---- */
static void test_take_drop(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var lst = [1,2,3,4,5]\n"
        "var t = slices.take(lst, 3)\n"
        "assert(len(t) == 3)\n"
        "assert(t[2] == 3)\n"
        "var d = slices.drop(lst, 2)\n"
        "assert(len(d) == 3)\n"
        "assert(d[0] == 3)\n"
    );
}

/* ---- take_while / drop_while ---- */
static void test_take_drop_while(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var lst = [1,2,3,4,5]\n"
        "var tw = slices.take_while(lst, fun(x){ return x < 4 })\n"
        "assert(len(tw) == 3)\n"
        "var dw = slices.drop_while(lst, fun(x){ return x < 4 })\n"
        "assert(len(dw) == 2)\n"
        "assert(dw[0] == 4)\n"
    );
}

/* ---- max / min / sum ---- */
static void test_stats(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var nums = [3,1,4,1,5,9,2,6]\n"
        "assert(slices.max(nums) == 9)\n"
        "assert(slices.min(nums) == 1)\n"
        "assert(slices.sum(nums) == 31)\n"
    );
    /* max/min empty list = nil */
    RUN_OK(
        "import \"slices\"\n"
        "assert(slices.max([]) == nil)\n"
        "assert(slices.min([]) == nil)\n"
    );
    /* sum with non-numeric = error */
    RUN_ERR("import \"slices\"\nslices.sum([1,\"oops\",3])\n");
}

/* ---- max/min with key fn ---- */
static void test_max_min_key(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var people = [{\"age\":30},{\"age\":25},{\"age\":40}]\n"
        "var oldest = slices.max(people, fun(p){ return p[\"age\"] })\n"
        "assert(oldest[\"age\"] == 40)\n"
        "var youngest = slices.min(people, fun(p){ return p[\"age\"] })\n"
        "assert(youngest[\"age\"] == 25)\n"
    );
}

/* ---- sorted ---- */
static void test_sorted(void) {
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.sorted([3,1,4,1,5,9,2,6])\n"
        "assert(len(r) == 8)\n"
        "assert(r[0] == 1)\n"
        "assert(r[7] == 9)\n"
    );
    /* reverse */
    RUN_OK(
        "import \"slices\"\n"
        "var r = slices.sorted([3,1,4], nil, true)\n"
        "assert(r[0] == 4)\n"
        "assert(r[2] == 1)\n"
    );
    /* with key fn */
    RUN_OK(
        "import \"slices\"\n"
        "var people = [{\"age\":30},{\"age\":25},{\"age\":40}]\n"
        "var s = slices.sorted(people, fun(p){ return p[\"age\"] })\n"
        "assert(s[0][\"age\"] == 25)\n"
        "assert(s[2][\"age\"] == 40)\n"
    );
}

/* ---- is_sorted ---- */
static void test_is_sorted(void) {
    RUN_OK(
        "import \"slices\"\n"
        "assert(slices.is_sorted([1,2,3,4,5]) == true)\n"
        "assert(slices.is_sorted([1,3,2,4]) == false)\n"
        "assert(slices.is_sorted([]) == true)\n"
        "assert(slices.is_sorted([42]) == true)\n"
    );
}

int main(void) {
    test_map();
    test_filter();
    test_flat_map();
    test_reduce();
    test_any_all();
    test_count();
    test_group_by();
    test_partition();
    test_find();
    test_index();
    test_reverse();
    test_reverse_in_place();
    test_flatten();
    test_unique();
    test_chunk();
    test_zip();
    test_enumerate();
    test_concat();
    test_take_drop();
    test_take_drop_while();
    test_stats();
    test_max_min_key();
    test_sorted();
    test_is_sorted();
    printf("test_stdlib_slices: all passed\n");
    return 0;
}
