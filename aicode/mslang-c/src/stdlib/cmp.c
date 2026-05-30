/* src/stdlib/cmp.c - STDLIB-35: cmp module
 *
 * Comparison primitives and comparator combinators.
 *
 * Primitives (compare, less, greater, equal, min, max, clamp, natural) are
 * implemented as C natives to handle both numeric and string operands.
 *
 * Combinators (reversed, by_key, chain, null_last, null_first) are compiled
 * from embedded .ms source so they return proper MsObjClosure values.
 * The helper __ms_cmp__ is registered as a host global so the closures
 * can reference it at call time via OP_GETGLOBAL.
 *
 * Public API:
 *   cmp.compare(a, b)         → int    -1 / 0 / 1
 *   cmp.less(a, b)            → bool
 *   cmp.greater(a, b)         → bool
 *   cmp.equal(a, b)           → bool
 *   cmp.min(a, b)             → any
 *   cmp.max(a, b)             → any
 *   cmp.clamp(value, lo, hi)  → any
 *   cmp.natural()             → cmp_fn  (same as compare itself)
 *   cmp.reversed(fn)          → cmp_fn
 *   cmp.by_key(key_fn)        → cmp_fn
 *   cmp.chain(fns)            → cmp_fn  (fns is a list)
 *   cmp.null_last(fn)         → cmp_fn
 *   cmp.null_first(fn)        → cmp_fn
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/object.h"
#include "ms/value.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Natural three-way comparison (numbers + strings)                   */
/* ------------------------------------------------------------------ */

static int natural_cmp(MsValue a, MsValue b) {
    if (MS_IS_INT(a) && MS_IS_INT(b)) {
        ms_i64 ia = MS_AS_INT(a), ib = MS_AS_INT(b);
        return (ia > ib) - (ia < ib);
    }
    if (MS_IS_NUMERIC(a) && MS_IS_NUMERIC(b)) {
        double da = ms_as_double(a), db = ms_as_double(b);
        return (da > db) - (da < db);
    }
    if (MS_IS_STRING(a) && MS_IS_STRING(b)) {
        int c = strcmp(MS_AS_CSTRING(a), MS_AS_CSTRING(b));
        return (c > 0) - (c < 0);
    }
    /* fallback: coerce both to double */
    double da = MS_IS_NUMERIC(a) ? ms_as_double(a) : 0.0;
    double db = MS_IS_NUMERIC(b) ? ms_as_double(b) : 0.0;
    return (da > db) - (da < db);
}

/* ------------------------------------------------------------------ */
/* C natives for primitives                                            */
/* ------------------------------------------------------------------ */

static MsValue cmp_compare(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "cmp.compare: expected 2 arguments");
        return MS_NIL_VAL();
    }
    return MS_INT_VAL((ms_i64)natural_cmp(argv[0], argv[1]));
}

static MsValue cmp_less(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "cmp.less: expected 2 arguments");
        return MS_NIL_VAL();
    }
    return MS_BOOL_VAL(natural_cmp(argv[0], argv[1]) < 0);
}

static MsValue cmp_greater(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "cmp.greater: expected 2 arguments");
        return MS_NIL_VAL();
    }
    return MS_BOOL_VAL(natural_cmp(argv[0], argv[1]) > 0);
}

static MsValue cmp_equal(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "cmp.equal: expected 2 arguments");
        return MS_NIL_VAL();
    }
    return MS_BOOL_VAL(natural_cmp(argv[0], argv[1]) == 0);
}

static MsValue cmp_min(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "cmp.min: expected 2 arguments");
        return MS_NIL_VAL();
    }
    return natural_cmp(argv[0], argv[1]) <= 0 ? argv[0] : argv[1];
}

static MsValue cmp_max(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "cmp.max: expected 2 arguments");
        return MS_NIL_VAL();
    }
    return natural_cmp(argv[0], argv[1]) >= 0 ? argv[0] : argv[1];
}

static MsValue cmp_clamp(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 3) {
        ms_vm_runtime_error(vm, "cmp.clamp: expected 3 arguments");
        return MS_NIL_VAL();
    }
    MsValue v = argv[0], lo = argv[1], hi = argv[2];
    if (natural_cmp(v, lo) < 0) return lo;
    if (natural_cmp(v, hi) > 0) return hi;
    return v;
}

/* cmp.natural() returns the compare native itself */
static MsValue cmp_natural(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    MsObjString* key = ms_obj_string_copy(vm, "compare", 7);
    /* Look up compare from cmp module exports via globals — but we have no
       direct module reference here. Instead we use the host global __ms_cmp__. */
    MsValue fn;
    MsObjString* cmp_key = ms_obj_string_copy(vm, "__ms_cmp__", 10);
    if (ms_table_get(&vm->globals, cmp_key, &fn) && !MS_IS_NIL(fn))
        return fn;
    /* Fallback: look up "compare" from host globals if __ms_cmp__ not found */
    if (ms_table_get(&vm->globals, key, &fn))
        return fn;
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* __ms_cmp__ host global — the compare function exposed to .ms       */
/* combinators that reference it via OP_GETGLOBAL                     */
/* ------------------------------------------------------------------ */

static MsValue host_compare(MsVM* vm, int argc, MsValue* argv) {
    (void)vm;
    if (argc < 2) return MS_INT_VAL(0);
    return MS_INT_VAL((ms_i64)natural_cmp(argv[0], argv[1]));
}

/* ------------------------------------------------------------------ */
/* Embedded .ms source for combinators                                */
/* Combinators use __ms_cmp__ (a host global native) for comparison.  */
/* All closures capture only local variables (no global references     */
/* except __ms_cmp__ which persists in host globals).                 */
/* ------------------------------------------------------------------ */

static const char CMP_COMBINATORS[] =
    "var reversed = fun(fn) {\n"
    "    return fun(a, b) { return -(fn(a, b)) }\n"
    "}\n"
    "var by_key = fun(kf) {\n"
    "    return fun(a, b) { return __ms_cmp__(kf(a), kf(b)) }\n"
    "}\n"
    "var chain = fun(fns) {\n"
    "    return fun(a, b) {\n"
    "        var i = 0\n"
    "        while (i < fns.len()) {\n"
    "            var r = fns[i](a, b)\n"
    "            if (r != 0) { return r }\n"
    "            i = i + 1\n"
    "        }\n"
    "        return 0\n"
    "    }\n"
    "}\n"
    "var null_last = fun(fn) {\n"
    "    return fun(a, b) {\n"
    "        var an = (a == nil)\n"
    "        var bn = (b == nil)\n"
    "        if (an and bn) { return 0 }\n"
    "        if (an) { return 1 }\n"
    "        if (bn) { return -1 }\n"
    "        return fn(a, b)\n"
    "    }\n"
    "}\n"
    "var null_first = fun(fn) {\n"
    "    return fun(a, b) {\n"
    "        var an = (a == nil)\n"
    "        var bn = (b == nil)\n"
    "        if (an and bn) { return 0 }\n"
    "        if (an) { return -1 }\n"
    "        if (bn) { return 1 }\n"
    "        return fn(a, b)\n"
    "    }\n"
    "}\n";

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_cmp_primitives[] = {
    {"compare", cmp_compare, 2},
    {"less",    cmp_less,    2},
    {"greater", cmp_greater, 2},
    {"equal",   cmp_equal,   2},
    {"min",     cmp_min,     2},
    {"max",     cmp_max,     2},
    {"clamp",   cmp_clamp,   3},
    {"natural", cmp_natural, 0},
    {NULL, NULL, 0}
};

void ms_module_cmp_init(MsVM* vm, MsObjModule* mod) {
    /* 1. Register primitives as native exports */
    ms_module_register_natives(vm, mod, ms_cmp_primitives);

    /* 2. Register __ms_cmp__ in host globals so .ms combinators can call it */
    ms_vm_define_native(vm, "__ms_cmp__", host_compare, 2);

    /* 3. Compile and execute .ms source to populate combinator closures */
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, CMP_COMBINATORS, "<builtin:cmp>",
                                   diags, &diag_count, 8);
    if (!fn) {
        if (diag_count > 0)
            ms_vm_runtime_error(vm, "cmp: combinator compile error: %s",
                                diags[0].message);
        else
            ms_vm_runtime_error(vm, "cmp: combinator compile failed");
        return;
    }
    ms_vm_execute_module(vm, fn, mod);
}
