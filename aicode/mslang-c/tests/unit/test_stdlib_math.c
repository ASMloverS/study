/* tests/unit/test_stdlib_math.c - STDLIB-01: math module */
#include "../test_assert.h"
#include "ms/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Heap-allocate MsVM to avoid stack overflow (~300KB on Windows). */
static MsInterpretResult run_snippet(const char* src) {
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) { fprintf(stderr, "OOM\n"); exit(1); }
    ms_vm_init(vm);
    MsInterpretResult r = ms_vm_interpret(vm, src, "<test>");
    ms_vm_free(vm);
    free(vm);
    return r;
}

#define RUN_OK(src) TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK)

/* ---- basic ---- */

static void test_abs(void) {
    RUN_OK("import \"math\"\nassert(math.abs(-3.5) == 3.5)");
    RUN_OK("import \"math\"\nassert(math.abs(2.0)  == 2.0)");
}

static void test_floor_ceil_trunc(void) {
    RUN_OK("import \"math\"\nassert(math.floor(1.9) == 1.0)");
    RUN_OK("import \"math\"\nassert(math.floor(-1.1) == -2.0)");
    RUN_OK("import \"math\"\nassert(math.ceil(1.1)  == 2.0)");
    RUN_OK("import \"math\"\nassert(math.trunc(-1.9) == -1.0)");
}

static void test_round(void) {
    RUN_OK("import \"math\"\nassert(math.round(1.4) == 1.0)");
    RUN_OK("import \"math\"\nassert(math.round(1.6) == 2.0)");
    /* ties-to-even: 0.5 -> 0 (even), 1.5 -> 2 (even) */
    RUN_OK("import \"math\"\nassert(math.round(0.5) == 0.0)");
    RUN_OK("import \"math\"\nassert(math.round(1.5) == 2.0)");
}

static void test_sign(void) {
    RUN_OK("import \"math\"\nassert(math.sign(5.0)  ==  1)");
    RUN_OK("import \"math\"\nassert(math.sign(-3.0) == -1)");
    RUN_OK("import \"math\"\nassert(math.sign(0.0)  ==  0)");
}

static void test_fmod(void) {
    RUN_OK("import \"math\"\nassert(math.fmod(5.0, 3.0) == 2.0)");
}

/* ---- power / log ---- */

static void test_sqrt(void) {
    RUN_OK("import \"math\"\nassert(math.sqrt(9) == 3.0)");
    RUN_OK("import \"math\"\nassert(math.sqrt(0) == 0.0)");
}

static void test_pow(void) {
    RUN_OK("import \"math\"\nassert(math.pow(2.0, 10.0) == 1024.0)");
}

static void test_exp_log(void) {
    RUN_OK("import \"math\"\nassert(math.abs(math.log(math.exp(1.0)) - 1.0) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.log2(8.0)  == 3.0)");
    RUN_OK("import \"math\"\nassert(math.log10(100.0) == 2.0)");
}

static void test_hypot(void) {
    RUN_OK("import \"math\"\nassert(math.hypot(3.0, 4.0) == 5.0)");
}

/* ---- trig ---- */

static void test_trig(void) {
    RUN_OK("import \"math\"\nassert(math.abs(math.sin(0.0)) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.abs(math.cos(0.0) - 1.0) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.abs(math.tan(0.0)) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.abs(math.asin(0.0)) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.abs(math.acos(1.0)) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.abs(math.atan(0.0)) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.abs(math.atan2(0.0, 1.0)) < 1e-10)");
}

static void test_hyperbolic(void) {
    RUN_OK("import \"math\"\nassert(math.abs(math.sinh(0.0)) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.abs(math.cosh(0.0) - 1.0) < 1e-10)");
    RUN_OK("import \"math\"\nassert(math.abs(math.tanh(0.0)) < 1e-10)");
}

static void test_degrees_radians(void) {
    RUN_OK("import \"math\"\nassert(math.abs(math.degrees(math.PI) - 180.0) < 1e-8)");
    RUN_OK("import \"math\"\nassert(math.abs(math.radians(180.0) - math.PI) < 1e-8)");
}

/* ---- aggregate ---- */

static void test_min_max(void) {
    RUN_OK("import \"math\"\nassert(math.min(3.0, 1.0, 2.0) == 1.0)");
    RUN_OK("import \"math\"\nassert(math.max(3.0, 1.0, 2.0) == 3.0)");
}

static void test_clamp(void) {
    RUN_OK("import \"math\"\nassert(math.clamp(10.0, 0.0, 5.0) == 5.0)");
    RUN_OK("import \"math\"\nassert(math.clamp(-1.0, 0.0, 5.0) == 0.0)");
    RUN_OK("import \"math\"\nassert(math.clamp(3.0,  0.0, 5.0) == 3.0)");
}

static void test_sum(void) {
    RUN_OK("import \"math\"\nassert(math.sum([1.0, 2.0, 3.0]) == 6.0)");
    RUN_OK("import \"math\"\nassert(math.sum([]) == 0.0)");
}

/* ---- integer ---- */

static void test_gcd(void) {
    RUN_OK("import \"math\"\nassert(math.gcd(12, 8) == 4)");
    RUN_OK("import \"math\"\nassert(math.gcd(7,  3) == 1)");
    RUN_OK("import \"math\"\nassert(math.gcd(0,  5) == 5)");
}

static void test_lcm(void) {
    RUN_OK("import \"math\"\nassert(math.lcm(4, 6) == 12)");
    RUN_OK("import \"math\"\nassert(math.lcm(3, 7) == 21)");
}

static void test_is_nan_inf_finite(void) {
    RUN_OK("import \"math\"\nassert(math.is_nan(math.NAN) == true)");
    RUN_OK("import \"math\"\nassert(math.is_nan(1.0)      == false)");
    RUN_OK("import \"math\"\nassert(math.is_inf(math.INF) == true)");
    RUN_OK("import \"math\"\nassert(math.is_inf(1.0)      == false)");
    RUN_OK("import \"math\"\nassert(math.is_finite(1.0)      == true)");
    RUN_OK("import \"math\"\nassert(math.is_finite(math.INF) == false)");
    RUN_OK("import \"math\"\nassert(math.is_finite(math.NAN) == false)");
}

/* ---- random ---- */

static void test_random(void) {
    RUN_OK("import \"math\"\nvar r = math.random()\nassert(r >= 0.0)\nassert(r < 1.0)");
}

static void test_randint(void) {
    RUN_OK("import \"math\"\nmath.seed(42)\nvar x = math.randint(1, 6)\nassert(x >= 1)\nassert(x <= 6)");
}

static void test_seed(void) {
    RUN_OK("import \"math\"\nmath.seed(42)\nmath.seed(0)\nvar _ = math.random()");
}

/* ---- constants ---- */

static void test_constants(void) {
    RUN_OK("import \"math\"\nassert(math.PI > 3.14)\nassert(math.PI < 3.15)");
    RUN_OK("import \"math\"\nassert(math.E  > 2.71)\nassert(math.E  < 2.72)");
    RUN_OK("import \"math\"\nassert(math.TAU > 6.28)\nassert(math.TAU < 6.29)");
    RUN_OK("import \"math\"\nassert(math.is_inf(math.INF))");
    RUN_OK("import \"math\"\nassert(math.is_nan(math.NAN))");
}

int main(void) {
    test_abs();
    test_floor_ceil_trunc();
    test_round();
    test_sign();
    test_fmod();
    test_sqrt();
    test_pow();
    test_exp_log();
    test_hypot();
    test_trig();
    test_hyperbolic();
    test_degrees_radians();
    test_min_max();
    test_clamp();
    test_sum();
    test_gcd();
    test_lcm();
    test_is_nan_inf_finite();
    test_random();
    test_randint();
    test_seed();
    test_constants();
    printf("test_stdlib_math: all passed\n");
    return 0;
}
