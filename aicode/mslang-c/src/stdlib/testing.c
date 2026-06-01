/* src/stdlib/testing.c - STDLIB-45: testing module
 *
 * Lightweight test framework: register/run tests, assertions, skip, benchmark.
 *
 * Public API:
 *   testing.run(name, fn)          register a test
 *   testing.run_all()              run all registered tests → map
 *   testing.run_matching(pattern)  run tests whose name contains pattern → map
 *   testing.assert(cond, msg="")   fail if cond is false
 *   testing.assert_eq(a, b, msg="")
 *   testing.assert_ne(a, b, msg="")
 *   testing.assert_lt/le/gt/ge(a, b)
 *   testing.assert_raises(fn, exc=nil)
 *   testing.assert_no_raise(fn)
 *   testing.assert_str_eq(a, b)
 *   testing.assert_approx(a, b, eps=1e-9)
 *   testing.skip(reason="")        skip current test
 *   testing.fail(msg="")           mark test failed (soft)
 *   testing.bench(name, fn, n=1000) → map
 *   testing.bench_all()            → list
 *
 * Internal state is stored in a MsObjUserdata ("testing.State") exported
 * as "__state__" in the module's export table.
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/memory.h"
#include "ms/event_loop.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ================================================================
 * Registry userdata
 * ================================================================ */

#define TESTING_STATE_TAG "testing.State"

typedef struct {
    /* registered tests: parallel arrays */
    MsValue* test_names;
    MsValue* test_fns;
    int      test_count;
    int      test_cap;
    /* registered benches: parallel arrays */
    MsValue* bench_names;
    MsValue* bench_fns;
    int*     bench_ns;
    int      bench_count;
    int      bench_cap;
    /* per-test transient state (set/cleared around each ms_vm_call_sync) */
    bool     in_test;
    bool     test_skip;
    bool     test_fail;
    char     skip_reason[256];
    char     fail_msg[256];
} TestingState;

static void testing_state_finalize(void* d) {
    TestingState* s = (TestingState*)d;
    free(s->test_names);
    free(s->test_fns);
    free(s->bench_names);
    free(s->bench_fns);
    free(s->bench_ns);
}

static void testing_state_mark(MsVM* vm, void* d) {
    TestingState* s = (TestingState*)d;
    for (int i = 0; i < s->test_count; i++) {
        ms_mark_value(vm, s->test_names[i]);
        ms_mark_value(vm, s->test_fns[i]);
    }
    for (int i = 0; i < s->bench_count; i++) {
        ms_mark_value(vm, s->bench_names[i]);
        ms_mark_value(vm, s->bench_fns[i]);
    }
}

/* ================================================================
 * Helper: get state from the module cache
 * ================================================================ */

static TestingState* get_state(MsVM* vm) {
    char key_buf[] = "<builtin:testing>";
    MsObjString* key = ms_obj_string_copy(vm, key_buf, (int)(sizeof(key_buf) - 1));
    MsValue modval;
    if (!ms_table_get(&vm->module_cache, key, &modval)) return NULL;
    if (!MS_IS_MODULE(modval)) return NULL;
    MsObjModule* mod = MS_AS_MODULE(modval);
    MsObjString* sk = ms_obj_string_copy(vm, "__state__", 9);
    MsValue sv;
    if (!ms_table_get(&mod->exports, sk, &sv)) return NULL;
    if (!MS_IS_USERDATA(sv)) return NULL;
    MsObjUserdata* ud = MS_AS_USERDATA(sv);
    if (strcmp(ud->type_tag, TESTING_STATE_TAG) != 0) return NULL;
    return (TestingState*)ud->data;
}

/* ================================================================
 * map set helper
 * ================================================================ */

static void map_set_str(MsVM* vm, MsObjMap* m, const char* k, const char* v) {
    MsObjString* key = ms_obj_string_copy(vm, k, (int)strlen(k));
    MsObjString* val = ms_obj_string_copy(vm, v, (int)strlen(v));
    ms_vtable_set(&m->table, MS_OBJ_VAL(key), MS_OBJ_VAL(val));
}

static void map_set_int(MsVM* vm, MsObjMap* m, const char* k, ms_i64 v) {
    MsObjString* key = ms_obj_string_copy(vm, k, (int)strlen(k));
    ms_vtable_set(&m->table, MS_OBJ_VAL(key), MS_INT_VAL(v));
}

static void map_set_val(MsVM* vm, MsObjMap* m, const char* k, MsValue v) {
    MsObjString* key = ms_obj_string_copy(vm, k, (int)strlen(k));
    ms_vtable_set(&m->table, MS_OBJ_VAL(key), v);
}

/* ================================================================
 * Numeric coercion helper
 * ================================================================ */

static double to_double(MsValue v) {
    if (MS_IS_INT(v))    return (double)v.as.integer;
    if (MS_IS_NUMBER(v)) return v.as.number;
    return 0.0;
}

/* ================================================================
 * Values-equal helper
 * ================================================================ */

static bool ms_vals_equal(MsValue a, MsValue b) {
    if (a.type != b.type) {
        /* int/number cross-compare */
        if ((MS_IS_INT(a) || MS_IS_NUMBER(a)) && (MS_IS_INT(b) || MS_IS_NUMBER(b)))
            return to_double(a) == to_double(b);
        return false;
    }
    switch (a.type) {
        case MS_VAL_NIL:    return true;
        case MS_VAL_BOOL:   return a.as.boolean == b.as.boolean;
        case MS_VAL_NUMBER: return a.as.number  == b.as.number;
        case MS_VAL_INT:    return a.as.integer == b.as.integer;
        case MS_VAL_OBJECT: {
            if (MS_IS_STRING(a) && MS_IS_STRING(b))
                return MS_AS_STRING(a) == MS_AS_STRING(b);   /* interned */
            return a.as.object == b.as.object;
        }
        default: return false;
    }
}

/* ================================================================
 * testing.assert(cond, msg="")
 * ================================================================ */

static MsValue testing_assert(MsVM* vm, int argc, MsValue* argv) {
    bool cond = MS_IS_BOOL(argv[0]) ? argv[0].as.boolean
              : !MS_IS_NIL(argv[0]);
    if (!cond) {
        const char* msg = (argc >= 2 && MS_IS_STRING(argv[1]))
                          ? MS_AS_CSTRING(argv[1]) : "";
        if (msg[0])
            ms_vm_runtime_error(vm, "testing.assert failed: %s", msg);
        else
            ms_vm_runtime_error(vm, "testing.assert failed");
    }
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.assert_eq(a, b, msg="")
 * ================================================================ */

static MsValue testing_assert_eq(MsVM* vm, int argc, MsValue* argv) {
    if (!ms_vals_equal(argv[0], argv[1])) {
        const char* msg = (argc >= 3 && MS_IS_STRING(argv[2]))
                          ? MS_AS_CSTRING(argv[2]) : "";
        if (msg[0])
            ms_vm_runtime_error(vm, "assert_eq failed: %s", msg);
        else
            ms_vm_runtime_error(vm, "assert_eq: values not equal");
    }
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.assert_ne(a, b, msg="")
 * ================================================================ */

static MsValue testing_assert_ne(MsVM* vm, int argc, MsValue* argv) {
    if (ms_vals_equal(argv[0], argv[1])) {
        const char* msg = (argc >= 3 && MS_IS_STRING(argv[2]))
                          ? MS_AS_CSTRING(argv[2]) : "";
        if (msg[0])
            ms_vm_runtime_error(vm, "assert_ne failed: %s", msg);
        else
            ms_vm_runtime_error(vm, "assert_ne: values are equal");
    }
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.assert_lt/le/gt/ge(a, b)
 * ================================================================ */

static MsValue testing_assert_lt(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!(to_double(argv[0]) < to_double(argv[1])))
        ms_vm_runtime_error(vm, "assert_lt: %g >= %g",
                            to_double(argv[0]), to_double(argv[1]));
    return MS_NIL_VAL();
}

static MsValue testing_assert_le(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!(to_double(argv[0]) <= to_double(argv[1])))
        ms_vm_runtime_error(vm, "assert_le: %g > %g",
                            to_double(argv[0]), to_double(argv[1]));
    return MS_NIL_VAL();
}

static MsValue testing_assert_gt(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!(to_double(argv[0]) > to_double(argv[1])))
        ms_vm_runtime_error(vm, "assert_gt: %g <= %g",
                            to_double(argv[0]), to_double(argv[1]));
    return MS_NIL_VAL();
}

static MsValue testing_assert_ge(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!(to_double(argv[0]) >= to_double(argv[1])))
        ms_vm_runtime_error(vm, "assert_ge: %g < %g",
                            to_double(argv[0]), to_double(argv[1]));
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.assert_raises(fn, exc=nil)
 * ================================================================ */

static MsValue testing_assert_raises(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsValue result;
    MsInterpretResult r = ms_vm_call_sync(vm, argv[0], NULL, 0, &result);
    if (r == MS_INTERPRET_OK) {
        ms_vm_runtime_error(vm, "assert_raises: function did not raise");
    } else {
        vm->had_runtime_error = false;
    }
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.assert_no_raise(fn)
 * ================================================================ */

static MsValue testing_assert_no_raise(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsValue result;
    MsInterpretResult r = ms_vm_call_sync(vm, argv[0], NULL, 0, &result);
    if (r != MS_INTERPRET_OK) {
        vm->had_runtime_error = false;
        ms_vm_runtime_error(vm, "assert_no_raise: function raised an exception");
    }
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.assert_str_eq(a, b)
 * ================================================================ */

static MsValue testing_assert_str_eq(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "assert_str_eq: both arguments must be strings");
        return MS_NIL_VAL();
    }
    const char* a = MS_AS_CSTRING(argv[0]);
    const char* b = MS_AS_CSTRING(argv[1]);
    if (strcmp(a, b) != 0)
        ms_vm_runtime_error(vm, "assert_str_eq: \"%s\" != \"%s\"", a, b);
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.assert_approx(a, b, eps=1e-9)
 * ================================================================ */

static MsValue testing_assert_approx(MsVM* vm, int argc, MsValue* argv) {
    double a   = to_double(argv[0]);
    double b   = to_double(argv[1]);
    double eps = (argc >= 3) ? to_double(argv[2]) : 1e-9;
    if (fabs(a - b) > eps)
        ms_vm_runtime_error(vm,
            "assert_approx: |%g - %g| = %g > eps %g", a, b, fabs(a - b), eps);
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.skip(reason="")
 * ================================================================ */

static MsValue testing_skip(MsVM* vm, int argc, MsValue* argv) {
    TestingState* s = get_state(vm);
    if (!s || !s->in_test) {
        ms_vm_runtime_error(vm, "testing.skip: not inside a test");
        return MS_NIL_VAL();
    }
    s->test_skip = true;
    const char* reason = (argc >= 1 && MS_IS_STRING(argv[0]))
                         ? MS_AS_CSTRING(argv[0]) : "";
    snprintf(s->skip_reason, sizeof(s->skip_reason), "%s", reason);
    /* raise to abort test function; run_all detects test_skip and ignores the error */
    ms_vm_runtime_error(vm, "__testing_skip__");
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.fail(msg="")
 * ================================================================ */

static MsValue testing_fail(MsVM* vm, int argc, MsValue* argv) {
    TestingState* s = get_state(vm);
    if (!s || !s->in_test) {
        ms_vm_runtime_error(vm, "testing.fail: not inside a test");
        return MS_NIL_VAL();
    }
    s->test_fail = true;
    const char* msg = (argc >= 1 && MS_IS_STRING(argv[0]))
                      ? MS_AS_CSTRING(argv[0]) : "fail() called";
    snprintf(s->fail_msg, sizeof(s->fail_msg), "%s", msg);
    return MS_NIL_VAL();
}

/* ================================================================
 * testing.run(name, fn)
 * ================================================================ */

static MsValue testing_run(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "testing.run: name must be a string");
        return MS_NIL_VAL();
    }
    TestingState* s = get_state(vm);
    if (!s) {
        ms_vm_runtime_error(vm, "testing.run: state not found");
        return MS_NIL_VAL();
    }
    if (s->test_count == s->test_cap) {
        int nc = (s->test_cap < 8) ? 8 : s->test_cap * 2;
        MsValue* nn = (MsValue*)realloc(s->test_names, (size_t)nc * sizeof(MsValue));
        if (!nn) { ms_vm_runtime_error(vm, "testing.run: OOM"); return MS_NIL_VAL(); }
        s->test_names = nn;
        MsValue* nf = (MsValue*)realloc(s->test_fns, (size_t)nc * sizeof(MsValue));
        if (!nf) { ms_vm_runtime_error(vm, "testing.run: OOM"); return MS_NIL_VAL(); }
        s->test_fns = nf;
        s->test_cap = nc;
    }
    s->test_names[s->test_count] = argv[0];
    s->test_fns  [s->test_count] = argv[1];
    s->test_count++;
    return MS_NIL_VAL();
}

/* ================================================================
 * run_tests_matching: shared by run_all and run_matching
 * ================================================================ */

static MsValue run_tests_matching(MsVM* vm, const char* pattern) {
    TestingState* s = get_state(vm);
    if (!s) {
        ms_vm_runtime_error(vm, "testing.run_all: state not found");
        return MS_NIL_VAL();
    }

    ms_i64 passed  = 0;
    ms_i64 failed  = 0;
    ms_i64 skipped = 0;

    MsObjList* errors = ms_obj_list_new(vm);
    uint64_t t0 = ms_monotonic_ms();

    for (int i = 0; i < s->test_count; i++) {
        const char* name = MS_AS_CSTRING(s->test_names[i]);
        if (pattern && strstr(name, pattern) == NULL) continue;

        s->in_test        = true;
        s->test_skip      = false;
        s->test_fail      = false;
        s->skip_reason[0] = '\0';
        s->fail_msg[0]    = '\0';

        MsValue result;
        MsInterpretResult r = ms_vm_call_sync(vm, s->test_fns[i], NULL, 0, &result);
        s->in_test = false;

        if (s->test_skip) {
            vm->had_runtime_error = false;
            skipped++;
            continue;
        }
        if (r != MS_INTERPRET_OK || s->test_fail) {
            vm->had_runtime_error = false;
            MsObjMap* entry = ms_obj_map_new(vm);
            map_set_val(vm, entry, "name", s->test_names[i]);
            const char* errmsg = s->test_fail ? s->fail_msg : "runtime error";
            map_set_str(vm, entry, "message", errmsg);
            MS_ARRAY_PUSH(&errors->items, MS_OBJ_VAL(entry), MsValue);
            failed++;
            continue;
        }
        passed++;
    }

    uint64_t elapsed = ms_monotonic_ms() - t0;

    MsObjMap* summary = ms_obj_map_new(vm);
    map_set_int(vm, summary, "passed",   passed);
    map_set_int(vm, summary, "failed",   failed);
    map_set_int(vm, summary, "skipped",  skipped);
    map_set_int(vm, summary, "total_ms", (ms_i64)elapsed);
    map_set_val(vm, summary, "errors",   MS_OBJ_VAL(errors));

    return MS_OBJ_VAL(summary);
}

/* ================================================================
 * testing.run_all()
 * ================================================================ */

static MsValue testing_run_all(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    return run_tests_matching(vm, NULL);
}

/* ================================================================
 * testing.run_matching(pattern)
 * ================================================================ */

static MsValue testing_run_matching(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "testing.run_matching: pattern must be a string");
        return MS_NIL_VAL();
    }
    return run_tests_matching(vm, MS_AS_CSTRING(argv[0]));
}

/* ================================================================
 * testing.bench(name, fn, n=1000)
 * ================================================================ */

static MsValue testing_bench(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "testing.bench: name must be a string");
        return MS_NIL_VAL();
    }
    int n = 1000;
    if (argc >= 3) {
        if (MS_IS_INT(argv[2]))    n = (int)argv[2].as.integer;
        else if (MS_IS_NUMBER(argv[2])) n = (int)argv[2].as.number;
    }
    if (n <= 0) n = 1;

    TestingState* s = get_state(vm);
    if (!s) {
        ms_vm_runtime_error(vm, "testing.bench: state not found");
        return MS_NIL_VAL();
    }

    /* also register for bench_all */
    if (s->bench_count == s->bench_cap) {
        int nc = (s->bench_cap < 8) ? 8 : s->bench_cap * 2;
        MsValue* nn = (MsValue*)realloc(s->bench_names, (size_t)nc * sizeof(MsValue));
        if (!nn) { ms_vm_runtime_error(vm, "testing.bench: OOM"); return MS_NIL_VAL(); }
        s->bench_names = nn;
        MsValue* nf = (MsValue*)realloc(s->bench_fns, (size_t)nc * sizeof(MsValue));
        if (!nf) { ms_vm_runtime_error(vm, "testing.bench: OOM"); return MS_NIL_VAL(); }
        s->bench_fns = nf;
        int* ni = (int*)realloc(s->bench_ns, (size_t)nc * sizeof(int));
        if (!ni) { ms_vm_runtime_error(vm, "testing.bench: OOM"); return MS_NIL_VAL(); }
        s->bench_ns  = ni;
        s->bench_cap = nc;
    }
    s->bench_names[s->bench_count] = argv[0];
    s->bench_fns  [s->bench_count] = argv[1];
    s->bench_ns   [s->bench_count] = n;
    s->bench_count++;

    /* run immediately and return result map */
    uint64_t t0 = ms_monotonic_ms();
    for (int j = 0; j < n; j++) {
        MsValue res;
        if (ms_vm_call_sync(vm, argv[1], NULL, 0, &res) != MS_INTERPRET_OK) {
            vm->had_runtime_error = false;
            ms_vm_runtime_error(vm, "testing.bench: benchmark function raised");
            return MS_NIL_VAL();
        }
    }
    uint64_t total_ms = ms_monotonic_ms() - t0;

    MsObjMap* result = ms_obj_map_new(vm);
    map_set_val(vm, result, "name",     argv[0]);
    map_set_int(vm, result, "n",        (ms_i64)n);
    map_set_int(vm, result, "total_ms", (ms_i64)total_ms);

    /* avg_ms as number */
    double avg = (n > 0) ? (double)total_ms / (double)n : 0.0;
    MsObjString* ak = ms_obj_string_copy(vm, "avg_ms", 6);
    ms_vtable_set(&result->table, MS_OBJ_VAL(ak), MS_NUMBER_VAL(avg));

    return MS_OBJ_VAL(result);
}

/* ================================================================
 * testing.bench_all()
 * ================================================================ */

static MsValue testing_bench_all(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    TestingState* s = get_state(vm);
    if (!s) {
        ms_vm_runtime_error(vm, "testing.bench_all: state not found");
        return MS_NIL_VAL();
    }

    MsObjList* results = ms_obj_list_new(vm);
    int count = s->bench_count;  /* snapshot count before loop; bench() appends */
    for (int i = 0; i < count; i++) {
        /* run the bench without re-registering: call ms_vm_call_sync directly */
        int n = s->bench_ns[i];
        uint64_t t0 = ms_monotonic_ms();
        for (int j = 0; j < n; j++) {
            MsValue res;
            if (ms_vm_call_sync(vm, s->bench_fns[i], NULL, 0, &res)
                    != MS_INTERPRET_OK) {
                vm->had_runtime_error = false;
                ms_vm_runtime_error(vm, "testing.bench_all: bench '%s' raised",
                                    MS_AS_CSTRING(s->bench_names[i]));
                return MS_NIL_VAL();
            }
        }
        uint64_t total_ms = ms_monotonic_ms() - t0;
        MsObjMap* entry = ms_obj_map_new(vm);
        map_set_val(vm, entry, "name",     s->bench_names[i]);
        map_set_int(vm, entry, "n",        (ms_i64)n);
        map_set_int(vm, entry, "total_ms", (ms_i64)total_ms);
        double avg = (n > 0) ? (double)total_ms / (double)n : 0.0;
        MsObjString* ak = ms_obj_string_copy(vm, "avg_ms", 6);
        ms_vtable_set(&entry->table, MS_OBJ_VAL(ak), MS_NUMBER_VAL(avg));
        MS_ARRAY_PUSH(&results->items, MS_OBJ_VAL(entry), MsValue);
    }
    return MS_OBJ_VAL(results);
}

/* ================================================================
 * Module init
 * ================================================================ */

static const MsNativeDef testing_defs[] = {
    {"run",            testing_run,            2},
    {"run_all",        testing_run_all,        0},
    {"run_matching",   testing_run_matching,   1},
    {"assert",         testing_assert,         1},
    {"assert_eq",      testing_assert_eq,      2},
    {"assert_ne",      testing_assert_ne,      2},
    {"assert_lt",      testing_assert_lt,      2},
    {"assert_le",      testing_assert_le,      2},
    {"assert_gt",      testing_assert_gt,      2},
    {"assert_ge",      testing_assert_ge,      2},
    {"assert_raises",  testing_assert_raises,  1},
    {"assert_no_raise",testing_assert_no_raise,1},
    {"assert_str_eq",  testing_assert_str_eq,  2},
    {"assert_approx",  testing_assert_approx,  2},
    {"skip",           testing_skip,           0},
    {"fail",           testing_fail,           0},
    {"bench",          testing_bench,          2},
    {"bench_all",      testing_bench_all,      0},
    {NULL, NULL, 0}
};

void ms_module_testing_init(MsVM* vm, MsObjModule* mod) {
    /* allocate and export the state userdata */
    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(TestingState),
                                            testing_state_finalize,
                                            testing_state_mark,
                                            TESTING_STATE_TAG);
    TestingState* s = (TestingState*)ud->data;
    memset(s, 0, sizeof(TestingState));
    ms_module_export_value(vm, mod, "__state__", MS_OBJ_VAL(ud));

    /* register native functions */
    ms_module_register_natives(vm, mod, testing_defs);
}
