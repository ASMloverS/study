/* STDLIB-01: math module */
/* M_PI / M_E require _USE_MATH_DEFINES on MSVC */
#if defined(_MSC_VER) && !defined(_USE_MATH_DEFINES)
#  define _USE_MATH_DEFINES
#endif
#include "ms/stdlib_register.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include <math.h>
#include <stdlib.h>

/* ---- helpers ---- */

/* Extract numeric value from MsValue (int or float -> double). */
static double to_double(MsValue v) {
    return MS_IS_INT(v) ? (double)MS_AS_INT(v) : MS_AS_NUMBER(v);
}

/* ---- basic ---- */

static MsValue ms_math_abs(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(fabs(to_double(argv[0])));
}

static MsValue ms_math_floor(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(floor(to_double(argv[0])));
}

static MsValue ms_math_ceil(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(ceil(to_double(argv[0])));
}

static MsValue ms_math_round(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    double x = to_double(argv[0]);
    double fl = floor(x);
    double diff = x - fl;
    if (diff < 0.5) return MS_NUMBER_VAL(fl);
    if (diff > 0.5) return MS_NUMBER_VAL(fl + 1.0);
    /* ties-to-even: round to nearest even integer */
    double rounded = fl + 1.0;
    return MS_NUMBER_VAL(fmod(fl, 2.0) == 0.0 ? fl : rounded);
}

static MsValue ms_math_trunc(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(trunc(to_double(argv[0])));
}

static MsValue ms_math_sign(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    double x = to_double(argv[0]);
    if (x > 0.0) return MS_INT_VAL(1);
    if (x < 0.0) return MS_INT_VAL(-1);
    return MS_INT_VAL(0);
}

static MsValue ms_math_fmod(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(fmod(to_double(argv[0]), to_double(argv[1])));
}

/* ---- power / log ---- */

static MsValue ms_math_sqrt(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(sqrt(to_double(argv[0])));
}

static MsValue ms_math_pow(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(pow(to_double(argv[0]), to_double(argv[1])));
}

static MsValue ms_math_exp(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(exp(to_double(argv[0])));
}

static MsValue ms_math_log(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(log(to_double(argv[0])));
}

static MsValue ms_math_log2(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(log2(to_double(argv[0])));
}

static MsValue ms_math_log10(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(log10(to_double(argv[0])));
}

static MsValue ms_math_hypot(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(hypot(to_double(argv[0]), to_double(argv[1])));
}

/* ---- trig ---- */

static MsValue ms_math_sin(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(sin(to_double(argv[0]))); }
static MsValue ms_math_cos(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(cos(to_double(argv[0]))); }
static MsValue ms_math_tan(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(tan(to_double(argv[0]))); }
static MsValue ms_math_asin(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(asin(to_double(argv[0]))); }
static MsValue ms_math_acos(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(acos(to_double(argv[0]))); }
static MsValue ms_math_atan(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(atan(to_double(argv[0]))); }
static MsValue ms_math_atan2(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(atan2(to_double(argv[0]), to_double(argv[1]))); }
static MsValue ms_math_sinh(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(sinh(to_double(argv[0]))); }
static MsValue ms_math_cosh(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(cosh(to_double(argv[0]))); }
static MsValue ms_math_tanh(MsVM* vm, int argc, MsValue* argv) { (void)vm;(void)argc; return MS_NUMBER_VAL(tanh(to_double(argv[0]))); }

#ifndef M_PI
#  define M_PI  3.14159265358979323846
#endif

static MsValue ms_math_degrees(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(to_double(argv[0]) * (180.0 / M_PI));
}

static MsValue ms_math_radians(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(to_double(argv[0]) * (M_PI / 180.0));
}

/* ---- aggregate ---- */

static MsValue ms_math_min(MsVM* vm, int argc, MsValue* argv) {
    if (argc == 0) { ms_vm_runtime_error(vm, "math.min: no arguments"); return MS_NIL_VAL(); }
    double m = to_double(argv[0]);
    for (int i = 1; i < argc; i++) {
        double v = to_double(argv[i]);
        if (v < m) m = v;
    }
    return MS_NUMBER_VAL(m);
}

static MsValue ms_math_max(MsVM* vm, int argc, MsValue* argv) {
    if (argc == 0) { ms_vm_runtime_error(vm, "math.max: no arguments"); return MS_NIL_VAL(); }
    double m = to_double(argv[0]);
    for (int i = 1; i < argc; i++) {
        double v = to_double(argv[i]);
        if (v > m) m = v;
    }
    return MS_NUMBER_VAL(m);
}

static MsValue ms_math_clamp(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    double x  = to_double(argv[0]);
    double lo = to_double(argv[1]);
    double hi = to_double(argv[2]);
    if (x < lo) return MS_NUMBER_VAL(lo);
    if (x > hi) return MS_NUMBER_VAL(hi);
    return MS_NUMBER_VAL(x);
}

/* Kahan compensated sum over a List argument. */
static MsValue ms_math_sum(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "math.sum: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* lst = MS_AS_LIST(argv[0]);
    double sum = 0.0, comp = 0.0;
    for (int i = 0; i < lst->items.count; i++) {
        double y = to_double(lst->items.data[i]) - comp;
        double t = sum + y;
        comp = (t - sum) - y;
        sum  = t;
    }
    return MS_NUMBER_VAL(sum);
}

/* ---- integer ---- */

static MsValue ms_math_gcd(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    ms_i64 a = MS_AS_INT(argv[0]);
    ms_i64 b = MS_AS_INT(argv[1]);
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { ms_i64 t = b; b = a % b; a = t; }
    return MS_INT_VAL(a);
}

static MsValue ms_math_lcm(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ms_i64 a = MS_AS_INT(argv[0]);
    ms_i64 b = MS_AS_INT(argv[1]);
    if (a == 0 || b == 0) return MS_INT_VAL(0);
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    ms_i64 g = a;
    ms_i64 r = b;
    while (r) { ms_i64 t = r; r = g % r; g = t; }
    ms_i64 result = a / g * b;
    if (result < 0) { ms_vm_runtime_error(vm, "math.lcm: overflow"); return MS_NIL_VAL(); }
    return MS_INT_VAL(result);
}

static MsValue ms_math_is_nan(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    if (!MS_IS_NUMBER(argv[0])) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(isnan(MS_AS_NUMBER(argv[0])));
}

static MsValue ms_math_is_inf(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    if (!MS_IS_NUMBER(argv[0])) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(isinf(MS_AS_NUMBER(argv[0])) != 0);
}

static MsValue ms_math_is_finite(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    if (!MS_IS_NUMBER(argv[0])) return MS_BOOL_VAL(true); /* ints are finite */
    return MS_BOOL_VAL(isfinite(MS_AS_NUMBER(argv[0])) != 0);
}

/* ---- random ---- */

static MsValue ms_math_random(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc; (void)argv;
    return MS_NUMBER_VAL((double)rand() / ((double)RAND_MAX + 1.0));
}

static MsValue ms_math_randint(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ms_i64 lo = MS_AS_INT(argv[0]);
    ms_i64 hi = MS_AS_INT(argv[1]);
    if (lo > hi) { ms_vm_runtime_error(vm, "math.randint: lo > hi"); return MS_NIL_VAL(); }
    int range = (int)(hi - lo + 1);
    int x;
    int limit = RAND_MAX - (RAND_MAX % range);
    do { x = rand(); } while (x >= limit);
    return MS_INT_VAL(lo + (ms_i64)(x % range));
}

static MsValue ms_math_seed(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    srand((unsigned int)MS_AS_INT(argv[0]));
    return MS_NIL_VAL();
}

/* ---- registration ---- */

static const MsNativeDef ms_math_defs[] = {
    {"abs",       ms_math_abs,      1},
    {"floor",     ms_math_floor,    1},
    {"ceil",      ms_math_ceil,     1},
    {"round",     ms_math_round,    1},
    {"trunc",     ms_math_trunc,    1},
    {"sign",      ms_math_sign,     1},
    {"fmod",      ms_math_fmod,     2},
    {"sqrt",      ms_math_sqrt,     1},
    {"pow",       ms_math_pow,      2},
    {"exp",       ms_math_exp,      1},
    {"log",       ms_math_log,      1},
    {"log2",      ms_math_log2,     1},
    {"log10",     ms_math_log10,    1},
    {"hypot",     ms_math_hypot,    2},
    {"sin",       ms_math_sin,      1},
    {"cos",       ms_math_cos,      1},
    {"tan",       ms_math_tan,      1},
    {"asin",      ms_math_asin,     1},
    {"acos",      ms_math_acos,     1},
    {"atan",      ms_math_atan,     1},
    {"atan2",     ms_math_atan2,    2},
    {"sinh",      ms_math_sinh,     1},
    {"cosh",      ms_math_cosh,     1},
    {"tanh",      ms_math_tanh,     1},
    {"degrees",   ms_math_degrees,  1},
    {"radians",   ms_math_radians,  1},
    {"min",       ms_math_min,     -1},
    {"max",       ms_math_max,     -1},
    {"clamp",     ms_math_clamp,    3},
    {"sum",       ms_math_sum,      1},
    {"gcd",       ms_math_gcd,      2},
    {"lcm",       ms_math_lcm,      2},
    {"is_nan",    ms_math_is_nan,   1},
    {"is_inf",    ms_math_is_inf,   1},
    {"is_finite", ms_math_is_finite,1},
    {"random",    ms_math_random,   0},
    {"randint",   ms_math_randint,  2},
    {"seed",      ms_math_seed,     1},
    {NULL, NULL, 0}
};

void ms_module_math_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_math_defs);
#ifndef M_E
#  define M_E  2.71828182845904523536
#endif
    ms_module_export_value(vm, mod, "PI",  MS_NUMBER_VAL(M_PI));
    ms_module_export_value(vm, mod, "E",   MS_NUMBER_VAL(M_E));
    ms_module_export_value(vm, mod, "TAU", MS_NUMBER_VAL(2.0 * M_PI));
    ms_module_export_value(vm, mod, "INF", MS_NUMBER_VAL(INFINITY));
    ms_module_export_value(vm, mod, "NAN", MS_NUMBER_VAL(NAN));
}
