/* src/stdlib/random.c - STDLIB-24: random module (high-level)
 *
 * Composition layer over _rand (MT19937 primitive).
 *
 * Public API:
 *   random.seed(n=nil)                     → nil
 *   random.float()                         → num   [0.0, 1.0)
 *   random.uniform(lo, hi)                 → num   [lo, hi)
 *   random.int(lo, hi)                     → int   [lo, hi] closed
 *   random.gauss(mu=0.0, sigma=1.0)        → num   Normal(mu, sigma)
 *   random.choice(list)                    → any
 *   random.choices(list, k, weights=nil)   → list  with replacement
 *   random.sample(list, k)                 → list  without replacement
 *   random.shuffle(list)                   → nil   in-place
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/table.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* _rand helpers — looked up once at random module init time            */
/* ------------------------------------------------------------------ */

static MsValue get_rand_fn(MsVM* vm, const char* fname) {
    /* _rand is a registered builtin; ms_module_load handles caching */
    MsObjModule* rmod = ms_module_load(vm, "_rand", NULL);
    if (!rmod) return MS_NIL_VAL();
    MsObjString* fkey = ms_obj_string_copy(vm, fname, (int)strlen(fname));
    MsValue fn;
    if (!ms_table_get(&rmod->exports, fkey, &fn)) return MS_NIL_VAL();
    return fn;
}

/* Call a _rand native fn with args.  Returns MS_NIL_VAL on error. */
static MsValue call_rand(MsVM* vm, const char* fname, MsValue* args, int argc) {
    MsValue fn = get_rand_fn(vm, fname);
    if (MS_IS_NIL(fn)) {
        ms_vm_runtime_error(vm, "random: _rand.%s unavailable", fname);
        return MS_NIL_VAL();
    }
    MsValue result = MS_NIL_VAL();
    ms_vm_call_sync(vm, fn, args, argc, &result);
    return result;
}

static double rand_double(MsVM* vm) {
    MsValue r = call_rand(vm, "rand_float", NULL, 0);
    return MS_IS_NUMBER(r) ? MS_AS_NUMBER(r) : 0.0;
}

/* ------------------------------------------------------------------ */
/* random.seed(n=nil)                                                   */
/* ------------------------------------------------------------------ */
static MsValue random_seed(MsVM* vm, int argc, MsValue* argv) {
    MsValue arg = (argc >= 1) ? argv[0] : MS_NIL_VAL();
    return call_rand(vm, "seed", &arg, 1);
}

/* ------------------------------------------------------------------ */
/* random.float()                                                       */
/* ------------------------------------------------------------------ */
static MsValue random_float(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    return call_rand(vm, "rand_float", NULL, 0);
}

/* ------------------------------------------------------------------ */
/* random.uniform(lo, hi)                                               */
/* ------------------------------------------------------------------ */
static MsValue random_uniform(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_NUMERIC(argv[0]) || !MS_IS_NUMERIC(argv[1])) {
        ms_vm_runtime_error(vm, "random.uniform: expected (num, num)");
        return MS_NIL_VAL();
    }
    double lo = ms_as_double(argv[0]);
    double hi = ms_as_double(argv[1]);
    if (lo >= hi) {
        ms_vm_runtime_error(vm, "random.uniform: lo must be < hi");
        return MS_NIL_VAL();
    }
    return MS_NUMBER_VAL(lo + rand_double(vm) * (hi - lo));
}

/* ------------------------------------------------------------------ */
/* random.int(lo, hi)                                                   */
/* ------------------------------------------------------------------ */
static MsValue random_int(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ms_i64 lo = MS_IS_INT(argv[0]) ? MS_AS_INT(argv[0])
                                    : (ms_i64)ms_as_double(argv[0]);
    ms_i64 hi;
    if (argc < 2 || MS_IS_NIL(argv[1])) {
        hi = (ms_i64)INT64_MAX;
    } else {
        hi = MS_IS_INT(argv[1]) ? MS_AS_INT(argv[1])
                                 : (ms_i64)ms_as_double(argv[1]);
    }
    if (lo > hi) {
        ms_vm_runtime_error(vm, "random.int: lo > hi");
        return MS_NIL_VAL();
    }
    MsValue args[2] = {MS_INT_VAL(lo), MS_INT_VAL(hi)};
    return call_rand(vm, "rand_int", args, 2);
}

/* ------------------------------------------------------------------ */
/* random.gauss(mu, sigma) — Box-Muller transform                      */
/* ------------------------------------------------------------------ */
static MsValue random_gauss(MsVM* vm, int argc, MsValue* argv) {
    double mu    = (argc >= 1 && MS_IS_NUMERIC(argv[0])) ? ms_as_double(argv[0]) : 0.0;
    double sigma = (argc >= 2 && MS_IS_NUMERIC(argv[1])) ? ms_as_double(argv[1]) : 1.0;
    double u1, u2;
    do { u1 = rand_double(vm); } while (u1 == 0.0);
    u2 = rand_double(vm);
    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
    return MS_NUMBER_VAL(mu + sigma * z);
}

/* ------------------------------------------------------------------ */
/* random.choice(list)                                                  */
/* ------------------------------------------------------------------ */
static MsValue random_choice(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "random.choice: argument must be a list");
        return MS_NIL_VAL();
    }
    MsObjList* lst = MS_AS_LIST(argv[0]);
    int n = lst->items.count;
    if (n == 0) {
        ms_vm_runtime_error(vm, "random.choice: list is empty");
        return MS_NIL_VAL();
    }
    MsValue args[2] = {MS_INT_VAL(0), MS_INT_VAL((ms_i64)(n - 1))};
    MsValue idx = call_rand(vm, "rand_int", args, 2);
    if (!MS_IS_INT(idx)) return MS_NIL_VAL();
    return lst->items.data[(int)MS_AS_INT(idx)];
}

/* ------------------------------------------------------------------ */
/* random.choices(list, k, weights=nil)                                 */
/* ------------------------------------------------------------------ */
static MsValue random_choices(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "random.choices: first argument must be a list");
        return MS_NIL_VAL();
    }
    MsObjList* lst = MS_AS_LIST(argv[0]);
    int n = lst->items.count;
    if (n == 0) {
        ms_vm_runtime_error(vm, "random.choices: list is empty");
        return MS_NIL_VAL();
    }
    int k = (argc >= 2 && MS_IS_INT(argv[1])) ? (int)MS_AS_INT(argv[1]) : 1;
    MsValue weights_val = (argc >= 3) ? argv[2] : MS_NIL_VAL();
    MsObjList* result = ms_obj_list_new(vm);

    if (MS_IS_NIL(weights_val)) {
        MsValue args[2] = {MS_INT_VAL(0), MS_INT_VAL((ms_i64)(n - 1))};
        for (int i = 0; i < k; i++) {
            MsValue idx = call_rand(vm, "rand_int", args, 2);
            if (!MS_IS_INT(idx)) return MS_NIL_VAL();
            ms_value_array_push(&result->items, lst->items.data[(int)MS_AS_INT(idx)]);
        }
    } else {
        if (!MS_IS_LIST(weights_val)) {
            ms_vm_runtime_error(vm, "random.choices: weights must be a list or nil");
            return MS_NIL_VAL();
        }
        MsObjList* wlst = MS_AS_LIST(weights_val);
        if (wlst->items.count != (size_t)n) {
            ms_vm_runtime_error(vm,
                "random.choices: weights length (%d) != list length (%d)",
                (int)wlst->items.count, n);
            return MS_NIL_VAL();
        }
        double* cum = (double*)malloc((size_t)n * sizeof(double));
        if (!cum) { ms_vm_runtime_error(vm, "random.choices: OOM"); return MS_NIL_VAL(); }
        double total = 0.0;
        for (int i = 0; i < n; i++) {
            total += ms_as_double(wlst->items.data[i]);
            cum[i] = total;
        }
        for (int i = 0; i < k; i++) {
            double r = rand_double(vm) * total;
            int chosen = n - 1;
            for (int j = 0; j < n; j++) {
                if (r < cum[j]) { chosen = j; break; }
            }
            ms_value_array_push(&result->items, lst->items.data[chosen]);
        }
        free(cum);
    }
    return MS_OBJ_VAL(result);
}

/* ------------------------------------------------------------------ */
/* random.sample(list, k) — Fisher-Yates partial shuffle               */
/* ------------------------------------------------------------------ */
static MsValue random_sample(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "random.sample: first argument must be a list");
        return MS_NIL_VAL();
    }
    MsObjList* lst = MS_AS_LIST(argv[0]);
    int n = lst->items.count;
    int k = MS_IS_INT(argv[1]) ? (int)MS_AS_INT(argv[1]) : (int)ms_as_double(argv[1]);
    if (k < 0 || k > n) {
        ms_vm_runtime_error(vm, "random.sample: k (%d) out of range [0, %d]", k, n);
        return MS_NIL_VAL();
    }
    MsValue* tmp = (MsValue*)malloc((size_t)n * sizeof(MsValue));
    if (!tmp) { ms_vm_runtime_error(vm, "random.sample: OOM"); return MS_NIL_VAL(); }
    memcpy(tmp, lst->items.data, (size_t)n * sizeof(MsValue));

    MsObjList* result = ms_obj_list_new(vm);
    for (int i = 0; i < k; i++) {
        MsValue args[2] = {MS_INT_VAL((ms_i64)i), MS_INT_VAL((ms_i64)(n - 1))};
        MsValue idx_v = call_rand(vm, "rand_int", args, 2);
        int j = MS_IS_INT(idx_v) ? (int)MS_AS_INT(idx_v) : i;
        MsValue t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
        ms_value_array_push(&result->items, tmp[i]);
    }
    free(tmp);
    return MS_OBJ_VAL(result);
}

/* ------------------------------------------------------------------ */
/* random.shuffle(list) — in-place Fisher-Yates                        */
/* ------------------------------------------------------------------ */
static MsValue random_shuffle(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "random.shuffle: argument must be a list");
        return MS_NIL_VAL();
    }
    MsObjList* lst = MS_AS_LIST(argv[0]);
    int n = lst->items.count;
    for (int i = n - 1; i > 0; i--) {
        MsValue args[2] = {MS_INT_VAL(0), MS_INT_VAL((ms_i64)i)};
        MsValue idx_v = call_rand(vm, "rand_int", args, 2);
        int j = MS_IS_INT(idx_v) ? (int)MS_AS_INT(idx_v) : 0;
        MsValue t = lst->items.data[i];
        lst->items.data[i] = lst->items.data[j];
        lst->items.data[j] = t;
    }
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

void ms_module_random_init(MsVM* vm, MsObjModule* mod) {
    /* Eagerly load _rand so it's available when any random.* fn is called */
    ms_module_load(vm, "_rand", NULL);

    static const MsNativeDef defs[] = {
        {"seed",    random_seed,    -1},
        {"float",   random_float,    0},
        {"uniform", random_uniform,  2},
        {"int",     random_int,      2},
        {"gauss",   random_gauss,   -1},
        {"choice",  random_choice,   1},
        {"choices", random_choices, -1},
        {"sample",  random_sample,   2},
        {"shuffle", random_shuffle,  1},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);
}
