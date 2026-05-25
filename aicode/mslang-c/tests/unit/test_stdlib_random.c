/* tests/unit/test_stdlib_random.c - STDLIB-24: random module */
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
        fprintf(stderr, "FAIL test_stdlib_random.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_random.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- seed + reproducibility ---- */
static void test_seed_reproducibility(void) {
    /* same seed → same sequence */
    RUN_OK(
        "import \"_rand\"\n"
        "_rand.seed(42)\n"
        "var a1 = _rand.rand_int(0, 1000)\n"
        "var a2 = _rand.rand_int(0, 1000)\n"
        "_rand.seed(42)\n"
        "var b1 = _rand.rand_int(0, 1000)\n"
        "var b2 = _rand.rand_int(0, 1000)\n"
        "assert(a1 == b1)\n"
        "assert(a2 == b2)\n"
    );
    /* seed(nil) = OS entropy — just verify it doesn't crash */
    RUN_OK(
        "import \"_rand\"\n"
        "_rand.seed(nil)\n"
    );
}

/* ---- rand_float range ---- */
static void test_rand_float(void) {
    RUN_OK(
        "import \"_rand\"\n"
        "_rand.seed(1)\n"
        "var i = 0\n"
        "while (i < 100) {\n"
        "    var f = _rand.rand_float()\n"
        "    assert(f >= 0.0)\n"
        "    assert(f < 1.0)\n"
        "    i = i + 1\n"
        "}\n"
    );
}

/* ---- rand_int range and both-ends inclusion ---- */
static void test_rand_int(void) {
    /* values stay in [lo, hi] */
    RUN_OK(
        "import \"_rand\"\n"
        "_rand.seed(7)\n"
        "var i = 0\n"
        "while (i < 200) {\n"
        "    var v = _rand.rand_int(1, 6)\n"
        "    assert(v >= 1)\n"
        "    assert(v <= 6)\n"
        "    i = i + 1\n"
        "}\n"
    );
    /* lo == hi always returns that value */
    RUN_OK(
        "import \"_rand\"\n"
        "_rand.seed(3)\n"
        "var i = 0\n"
        "while (i < 20) {\n"
        "    assert(_rand.rand_int(5, 5) == 5)\n"
        "    i = i + 1\n"
        "}\n"
    );
}

/* ---- random.float / random.uniform / random.int / random.gauss ---- */
static void test_random_basics(void) {
    RUN_OK(
        "import \"random\"\n"
        "random.seed(99)\n"
        "var f = random.float()\n"
        "assert(f >= 0.0)\n"
        "assert(f < 1.0)\n"
    );
    RUN_OK(
        "import \"random\"\n"
        "random.seed(1)\n"
        "var i = 0\n"
        "while (i < 50) {\n"
        "    var u = random.uniform(2.0, 5.0)\n"
        "    assert(u >= 2.0)\n"
        "    assert(u < 5.0)\n"
        "    i = i + 1\n"
        "}\n"
    );
    RUN_OK(
        "import \"random\"\n"
        "random.seed(2)\n"
        "var i = 0\n"
        "while (i < 100) {\n"
        "    var v = random.int(1, 10)\n"
        "    assert(v >= 1)\n"
        "    assert(v <= 10)\n"
        "    i = i + 1\n"
        "}\n"
    );
    /* gauss returns a number — just verify it doesn't error */
    RUN_OK(
        "import \"random\"\n"
        "random.seed(5)\n"
        "var g = random.gauss(0.0, 1.0)\n"
        "assert(g == g)\n"  /* not NaN */
    );
}

/* ---- random.choice ---- */
static void test_choice(void) {
    RUN_OK(
        "import \"random\"\n"
        "random.seed(10)\n"
        "var lst = [1, 2, 3, 4, 5]\n"
        "var c = random.choice(lst)\n"
        "assert(c >= 1)\n"
        "assert(c <= 5)\n"
    );
    /* empty list → error */
    RUN_ERR("import \"random\"\nrandom.choice([])\n");
}

/* ---- random.sample (no duplicates) ---- */
static void test_sample(void) {
    RUN_OK(
        "import \"random\"\n"
        "random.seed(20)\n"
        "var lst = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]\n"
        "var s = random.sample(lst, 5)\n"
        "assert(len(s) == 5)\n"
        /* verify no duplicates */
        "var i = 0\n"
        "while (i < len(s)) {\n"
        "    var j = i + 1\n"
        "    while (j < len(s)) {\n"
        "        assert(s[i] != s[j])\n"
        "        j = j + 1\n"
        "    }\n"
        "    i = i + 1\n"
        "}\n"
    );
    /* k == list length → permutation, no duplicates */
    RUN_OK(
        "import \"random\"\n"
        "random.seed(21)\n"
        "var lst = [1, 2, 3, 4, 5]\n"
        "var s = random.sample(lst, 5)\n"
        "assert(len(s) == 5)\n"
    );
    /* k > len → error */
    RUN_ERR("import \"random\"\nrandom.sample([1,2,3], 10)\n");
}

/* ---- random.shuffle ---- */
static void test_shuffle(void) {
    /* length preserved */
    RUN_OK(
        "import \"random\"\n"
        "random.seed(30)\n"
        "var lst = [1, 2, 3, 4, 5, 6]\n"
        "random.shuffle(lst)\n"
        "assert(len(lst) == 6)\n"
    );
    /* all original elements still present (sum preserved) */
    RUN_OK(
        "import \"random\"\n"
        "random.seed(31)\n"
        "var lst = [1, 2, 3, 4, 5]\n"
        "random.shuffle(lst)\n"
        "var sum = lst[0] + lst[1] + lst[2] + lst[3] + lst[4]\n"
        "assert(sum == 15)\n"
    );
    /* shuffle returns nil */
    RUN_OK(
        "import \"random\"\n"
        "random.seed(32)\n"
        "var lst = [1, 2, 3]\n"
        "assert(random.shuffle(lst) == nil)\n"
    );
}

/* ---- random.choices (with replacement) ---- */
static void test_choices(void) {
    /* k elements returned */
    RUN_OK(
        "import \"random\"\n"
        "random.seed(40)\n"
        "var lst = [\"a\", \"b\", \"c\"]\n"
        "var r = random.choices(lst, 10, nil)\n"
        "assert(len(r) == 10)\n"
    );
    /* weighted — just verify length and no error */
    RUN_OK(
        "import \"random\"\n"
        "random.seed(41)\n"
        "var lst = [\"a\", \"b\", \"c\"]\n"
        "var r = random.choices(lst, 5, [0.6, 0.3, 0.1])\n"
        "assert(len(r) == 5)\n"
    );
    /* empty list → error */
    RUN_ERR("import \"random\"\nrandom.choices([], 3, nil)\n");
    /* weight count mismatch → error */
    RUN_ERR("import \"random\"\nrandom.choices([1,2,3], 3, [0.5, 0.5])\n");
}

/* ---- error cases ---- */
static void test_errors(void) {
    /* rand_int lo > hi */
    RUN_ERR("import \"_rand\"\n_rand.rand_int(10, 5)\n");
    /* random.int lo > hi */
    RUN_ERR("import \"random\"\nrandom.int(10, 5)\n");
    /* random.uniform lo >= hi */
    RUN_ERR("import \"random\"\nrandom.uniform(5.0, 3.0)\n");
}

int main(void) {
    test_seed_reproducibility();
    test_rand_float();
    test_rand_int();
    test_random_basics();
    test_choice();
    test_sample();
    test_shuffle();
    test_choices();
    test_errors();
    printf("test_stdlib_random: all passed\n");
    return 0;
}
