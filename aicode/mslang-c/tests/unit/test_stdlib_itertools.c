/* tests/unit/test_stdlib_itertools.c - STDLIB-23: itertools module */
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
        fprintf(stderr, "FAIL test_stdlib_itertools.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_itertools.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- take (materializes) ---- */
static void test_take(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.take(3, [10, 20, 30, 40])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 10)\n"
        "assert(r[2] == 30)\n"
    );
    /* take more than available: clamp */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.take(10, [1, 2, 3])\n"
        "assert(len(r) == 3)\n"
    );
    /* take 0 */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.take(0, [1, 2, 3])\n"
        "assert(len(r) == 0)\n"
    );
}

/* ---- reduce ---- */
static void test_reduce(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "fun add(a, b) { return a + b }\n"
        "var r = itertools.reduce(add, [1, 2, 3, 4], 0)\n"
        "assert(r == 10)\n"
    );
    RUN_OK(
        "import \"itertools\"\n"
        "fun mul(a, b) { return a * b }\n"
        "var r = itertools.reduce(mul, [1, 2, 3, 4], 1)\n"
        "assert(r == 24)\n"
    );
    /* empty list returns init */
    RUN_OK(
        "import \"itertools\"\n"
        "fun add(a, b) { return a + b }\n"
        "var r = itertools.reduce(add, [], 42)\n"
        "assert(r == 42)\n"
    );
}

/* ---- chain ---- */
static void test_chain(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.chain([1, 2], [3, 4], [5])\n"
        "assert(len(r) == 5)\n"
        "assert(r[0] == 1)\n"
        "assert(r[4] == 5)\n"
    );
    /* empty chains */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.chain([], [1], [])\n"
        "assert(len(r) == 1)\n"
        "assert(r[0] == 1)\n"
    );
}

/* ---- zip_iter ---- */
static void test_zip_iter(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.zip_iter([1, 2, 3], [\"a\", \"b\", \"c\"])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0][0] == 1)\n"
        "assert(r[0][1] == \"a\")\n"
        "assert(r[2][1] == \"c\")\n"
    );
    /* shortest wins */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.zip_iter([1, 2, 3], [\"a\", \"b\"])\n"
        "assert(len(r) == 2)\n"
    );
}

/* ---- zip_longest ---- */
static void test_zip_longest(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.zip_longest([1, 2, 3], [\"a\", \"b\"], 0)\n"
        "assert(len(r) == 3)\n"
        "assert(r[2][1] == 0)\n"  /* fill value */
    );
}

/* ---- enumerate ---- */
static void test_enumerate(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.enumerate([\"x\", \"y\", \"z\"], 0)\n"
        "assert(len(r) == 3)\n"
        "assert(r[0][0] == 0)\n"
        "assert(r[0][1] == \"x\")\n"
        "assert(r[2][0] == 2)\n"
    );
    /* custom start */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.enumerate([\"a\", \"b\"], 5)\n"
        "assert(r[0][0] == 5)\n"
        "assert(r[1][0] == 6)\n"
    );
}

/* ---- flatten ---- */
static void test_flatten(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.flatten([[1, 2], [3], [4, 5]])\n"
        "assert(len(r) == 5)\n"
        "assert(r[0] == 1)\n"
        "assert(r[4] == 5)\n"
    );
    /* non-list items passed through */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.flatten([[1], 2, [3]])\n"
        "assert(len(r) == 3)\n"
        "assert(r[1] == 2)\n"
    );
}

/* ---- windowed ---- */
static void test_windowed(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.windowed([1, 2, 3, 4, 5], 3)\n"
        "assert(len(r) == 3)\n"
        "assert(r[0][0] == 1)\n"
        "assert(r[0][2] == 3)\n"
        "assert(r[2][0] == 3)\n"
        "assert(r[2][2] == 5)\n"
    );
    /* window larger than list: empty */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.windowed([1, 2], 5)\n"
        "assert(len(r) == 0)\n"
    );
}

/* ---- batch ---- */
static void test_batch(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.batch([1, 2, 3, 4, 5], 2)\n"
        "assert(len(r) == 3)\n"
        "assert(len(r[0]) == 2)\n"
        "assert(len(r[2]) == 1)\n"  /* last batch is partial */
        "assert(r[2][0] == 5)\n"
    );
}

/* ---- accumulate ---- */
static void test_accumulate(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.accumulate([1, 2, 3, 4], nil)\n"
        "assert(len(r) == 4)\n"
        "assert(r[0] == 1)\n"
        "assert(r[1] == 3)\n"
        "assert(r[2] == 6)\n"
        "assert(r[3] == 10)\n"
    );
    /* custom fn: multiply */
    RUN_OK(
        "import \"itertools\"\n"
        "fun mul(a, b) { return a * b }\n"
        "var r = itertools.accumulate([1, 2, 3, 4], mul)\n"
        "assert(r[0] == 1)\n"
        "assert(r[1] == 2)\n"
        "assert(r[2] == 6)\n"
        "assert(r[3] == 24)\n"
    );
}

/* ---- pairwise ---- */
static void test_pairwise(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.pairwise([1, 2, 3, 4])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0][0] == 1)\n"
        "assert(r[0][1] == 2)\n"
        "assert(r[2][0] == 3)\n"
        "assert(r[2][1] == 4)\n"
    );
    /* less than 2 elements: empty */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.pairwise([42])\n"
        "assert(len(r) == 0)\n"
    );
}

/* ---- map ---- */
static void test_map(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "fun dbl(x) { return x * 2 }\n"
        "var r = itertools.map(dbl, [1, 2, 3])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 2)\n"
        "assert(r[2] == 6)\n"
    );
}

/* ---- filter ---- */
static void test_filter(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "fun even(x) { return x % 2 == 0 }\n"
        "var r = itertools.filter(even, [1, 2, 3, 4, 5, 6])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 2)\n"
        "assert(r[2] == 6)\n"
    );
}

/* ---- starmap ---- */
static void test_starmap(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "fun add(a, b) { return a + b }\n"
        "var r = itertools.starmap(add, [[1, 2], [3, 4], [5, 6]])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 3)\n"
        "assert(r[2] == 11)\n"
    );
}

/* ---- drop ---- */
static void test_drop(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.drop(2, [1, 2, 3, 4, 5])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 3)\n"
        "assert(r[2] == 5)\n"
    );
    /* drop more than length: empty */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.drop(10, [1, 2, 3])\n"
        "assert(len(r) == 0)\n"
    );
}

/* ---- take_while ---- */
static void test_take_while(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "fun lt5(x) { return x < 5 }\n"
        "var r = itertools.take_while(lt5, [1, 2, 3, 5, 6, 1])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 1)\n"
        "assert(r[2] == 3)\n"
    );
}

/* ---- drop_while ---- */
static void test_drop_while(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "fun lt5(x) { return x < 5 }\n"
        "var r = itertools.drop_while(lt5, [1, 2, 3, 5, 6, 1])\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 5)\n"
        "assert(r[2] == 1)\n"
    );
}

/* ---- groupby ---- */
static void test_groupby(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "fun key(x) { return x % 2 }\n"
        "var r = itertools.groupby([1, 3, 2, 4, 5], key)\n"
        "assert(len(r) == 3)\n"  /* [1,3] [2,4] [5] */
        "assert(r[0][0] == 1)\n"  /* key for odd */
        "assert(len(r[0][1]) == 2)\n"  /* [1,3] */
        "assert(r[1][0] == 0)\n"  /* key for even */
    );
}

/* ---- product ---- */
static void test_product(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.product([1, 2], [\"a\", \"b\"])\n"
        "assert(len(r) == 4)\n"
        "assert(r[0][0] == 1)\n"
        "assert(r[0][1] == \"a\")\n"
        "assert(r[3][0] == 2)\n"
        "assert(r[3][1] == \"b\")\n"
    );
}

/* ---- permutations ---- */
static void test_permutations(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.permutations([1, 2, 3], 2)\n"
        "assert(len(r) == 6)\n"
    );
    /* r=nil → full permutations */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.permutations([1, 2, 3], nil)\n"
        "assert(len(r) == 6)\n"
    );
}

/* ---- combinations ---- */
static void test_combinations(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.combinations([1, 2, 3, 4], 2)\n"
        "assert(len(r) == 6)\n"
    );
}

/* ---- combinations_with_replacement ---- */
static void test_combinations_with_replacement(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.combinations_with_replacement([1, 2, 3], 2)\n"
        "assert(len(r) == 6)\n"
    );
}

/* ---- infinite generators (return coroutine; use time.resume) ---- */
static void test_count(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "import \"time\"\n"
        "var g = itertools.count(10, 2)\n"
        "assert(time.resume(g) == 10)\n"
        "assert(time.resume(g) == 12)\n"
        "assert(time.resume(g) == 14)\n"
    );
    /* defaults: start=0, step=1 */
    RUN_OK(
        "import \"itertools\"\n"
        "import \"time\"\n"
        "var g = itertools.count(0, 1)\n"
        "assert(time.resume(g) == 0)\n"
        "assert(time.resume(g) == 1)\n"
        "assert(time.resume(g) == 2)\n"
    );
}

static void test_cycle(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "import \"time\"\n"
        "var g = itertools.cycle([1, 2, 3])\n"
        "assert(time.resume(g) == 1)\n"
        "assert(time.resume(g) == 2)\n"
        "assert(time.resume(g) == 3)\n"
        "assert(time.resume(g) == 1)\n"  /* wraps */
        "assert(time.resume(g) == 2)\n"
    );
}

static void test_repeat(void) {
    /* finite repeat → list */
    RUN_OK(
        "import \"itertools\"\n"
        "var r = itertools.repeat(42, 3)\n"
        "assert(len(r) == 3)\n"
        "assert(r[0] == 42)\n"
        "assert(r[2] == 42)\n"
    );
    /* infinite repeat → coroutine */
    RUN_OK(
        "import \"itertools\"\n"
        "import \"time\"\n"
        "var g = itertools.repeat(\"x\", nil)\n"
        "assert(time.resume(g) == \"x\")\n"
        "assert(time.resume(g) == \"x\")\n"
        "assert(time.resume(g) == \"x\")\n"
    );
}

/* ---- take with infinite generator ---- */
static void test_take_with_count(void) {
    RUN_OK(
        "import \"itertools\"\n"
        "import \"time\"\n"
        "var g = itertools.count(0, 1)\n"
        "var r = itertools.take_gen(5, g)\n"
        "assert(len(r) == 5)\n"
        "assert(r[0] == 0)\n"
        "assert(r[4] == 4)\n"
    );
}

/* ---- error cases ---- */
static void test_errors(void) {
    /* take: non-int n */
    RUN_ERR("import \"itertools\"\nitertools.take(\"bad\", [1,2])\n");
    /* reduce: non-callable fn */
    RUN_ERR("import \"itertools\"\nitertools.reduce(42, [1,2], 0)\n");
    /* chain: non-list arg */
    RUN_ERR("import \"itertools\"\nitertools.chain([1], 42)\n");
    /* map: non-callable fn */
    RUN_ERR("import \"itertools\"\nitertools.map(42, [1,2])\n");
    /* windowed: n=0 */
    RUN_ERR("import \"itertools\"\nitertools.windowed([1,2,3], 0)\n");
    /* batch: n=0 */
    RUN_ERR("import \"itertools\"\nitertools.batch([1,2,3], 0)\n");
    /* permutations: r > n */
    RUN_ERR("import \"itertools\"\nitertools.permutations([1,2], 5)\n");
    /* combinations: r > n */
    RUN_ERR("import \"itertools\"\nitertools.combinations([1,2], 5)\n");
}

int main(void) {
    test_take();
    test_reduce();
    test_chain();
    test_zip_iter();
    test_zip_longest();
    test_enumerate();
    test_flatten();
    test_windowed();
    test_batch();
    test_accumulate();
    test_pairwise();
    test_map();
    test_filter();
    test_starmap();
    test_drop();
    test_take_while();
    test_drop_while();
    test_groupby();
    test_product();
    test_permutations();
    test_combinations();
    test_combinations_with_replacement();
    test_count();
    test_cycle();
    test_repeat();
    test_take_with_count();
    test_errors();
    printf("test_stdlib_itertools: all passed\n");
    return 0;
}
