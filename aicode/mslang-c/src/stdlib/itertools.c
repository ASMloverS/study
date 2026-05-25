/* src/stdlib/itertools.c - STDLIB-23: itertools module
 *
 * Lazy-style iteration utilities.
 * Finite operations on lists return lists (materialized).
 * Infinite generators (count/cycle/repeat-nil) return coroutines.
 *
 * Public API (all take list inputs unless noted):
 *   -- Infinite (return coroutine; drive with time.resume) --
 *   itertools.count(start, step)          → coroutine
 *   itertools.cycle(list)                 → coroutine
 *   itertools.repeat(value, n=nil)        → list (finite) | coroutine (nil)
 *   -- Finite combinators (return list) --
 *   itertools.chain(list...)              → list
 *   itertools.zip_iter(list...)           → list
 *   itertools.zip_longest(list..., fill)  → list  (last arg = fill value)
 *   itertools.enumerate(list, start)      → list
 *   itertools.flatten(list)               → list
 *   itertools.windowed(list, n)           → list
 *   itertools.batch(list, n)              → list
 *   itertools.accumulate(list, fn)        → list
 *   itertools.pairwise(list)              → list
 *   -- Functional transforms --
 *   itertools.map(fn, list)               → list
 *   itertools.filter(fn, list)            → list
 *   itertools.starmap(fn, list)           → list
 *   itertools.take(n, list)               → list
 *   itertools.take_gen(n, coroutine)      → list
 *   itertools.drop(n, list)               → list
 *   itertools.take_while(fn, list)        → list
 *   itertools.drop_while(fn, list)        → list
 *   itertools.reduce(fn, list, init)      → any
 *   itertools.groupby(list, key)          → list of [key, list] pairs
 *   -- Combinatoric --
 *   itertools.product(list, list)         → list
 *   itertools.permutations(list, r)       → list
 *   itertools.combinations(list, r)       → list
 *   itertools.combinations_with_replacement(list, r) → list
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/table.h"
#include "ms/compiler.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static bool is_callable(MsValue v) {
    if (!MS_IS_OBJECT(v)) return false;
    MsObjectType t = MS_OBJ_TYPE(v);
    return t == MS_OBJ_CLOSURE || t == MS_OBJ_NATIVE || t == MS_OBJ_BOUND_METHOD;
}

static bool call1(MsVM* vm, MsValue fn, MsValue a, MsValue* out) {
    return ms_vm_call_sync(vm, fn, &a, 1, out) == MS_INTERPRET_OK;
}

static bool call2(MsVM* vm, MsValue fn, MsValue a, MsValue b, MsValue* out) {
    MsValue args[2] = {a, b};
    return ms_vm_call_sync(vm, fn, args, 2, out) == MS_INTERPRET_OK;
}

/* ------------------------------------------------------------------ */
/* Generator closure helpers                                            */
/* ------------------------------------------------------------------ */

/* Retrieve a stored generator closure from module exports by name. */
static MsObjClosure* get_gen_cl(MsVM* vm, MsObjModule* mod,
                                 const char* name) {
    MsObjString* key = ms_obj_string_copy(vm, name, (int)strlen(name));
    MsValue v = MS_NIL_VAL();
    ms_table_get(&mod->exports, key, &v);
    return MS_IS_CLOSURE(v) ? MS_AS_CLOSURE(v) : NULL;
}

/* Retrieve generator closure from the cached module in vm->module_cache. */
static MsObjClosure* get_stored_gen(MsVM* vm, const char* key_name) {
    MsObjString* mkey = ms_obj_string_copy(vm, "<builtin:itertools>",
                                            (int)strlen("<builtin:itertools>"));
    MsValue mv = MS_NIL_VAL();
    if (!ms_table_get(&vm->module_cache, mkey, &mv)) return NULL;
    if (!MS_IS_MODULE(mv)) return NULL;
    return get_gen_cl(vm, MS_AS_MODULE(mv), key_name);
}

/* Create a coroutine from a compiled generator closure, copying args.
   Mirrors the VM's MS_OP_CALL generator branch (vm.c lines 637-656). */
static MsValue make_coroutine(MsVM* vm, MsObjClosure* gen_cl,
                              MsValue* args, int argc) {
    MsObjFunction* fn = gen_cl->function;
    if (!fn->is_generator) {
        ms_vm_runtime_error(vm, "itertools: expected generator closure");
        return MS_NIL_VAL();
    }
    MsObjCoroutine* co = ms_obj_coroutine_new(vm, gen_cl);
    if (!co) return MS_NIL_VAL();
    /* Copy args into coroutine's initial stack slots */
    for (int i = 0; i < argc; i++)
        co->stack_buf[i] = args[i];
    /* Set up initial call frame */
    int need = fn->max_stack_size + 1;
    co->ctx.stack_top = co->ctx.stack + need;
    MsCallFrame* f = &co->ctx.frames[0];
    f->closure        = gen_cl;
    f->ip             = fn->chunk.code;
    f->slots          = co->ctx.stack;
    f->deferred       = NULL;
    f->deferred_count    = 0;
    f->deferred_capacity = 0;
    co->ctx.frame_count = 1;
    co->state = MS_CORO_CREATED;
    return MS_OBJ_VAL(co);
}

/* ------------------------------------------------------------------ */
/* Infinite generators                                                  */
/* ------------------------------------------------------------------ */

/* count(start, step): yields start, start+step, ... */
static MsValue it_count(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_NUMERIC(argv[0]) || !MS_IS_NUMERIC(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.count: expected (start, step)");
        return MS_NIL_VAL();
    }
    MsObjClosure* cl = get_stored_gen(vm, "_gen_count");
    if (!cl) { ms_vm_runtime_error(vm, "itertools.count: not initialized"); return MS_NIL_VAL(); }
    MsValue args[2] = {argv[0], argv[1]};
    return make_coroutine(vm, cl, args, 2);
}

/* cycle(list): yields elements cyclically */
static MsValue it_cycle(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "itertools.cycle: expected list");
        return MS_NIL_VAL();
    }
    if (MS_AS_LIST(argv[0])->items.count == 0) {
        ms_vm_runtime_error(vm, "itertools.cycle: list must be non-empty");
        return MS_NIL_VAL();
    }
    MsObjClosure* cl = get_stored_gen(vm, "_gen_cycle");
    if (!cl) { ms_vm_runtime_error(vm, "itertools.cycle: not initialized"); return MS_NIL_VAL(); }
    return make_coroutine(vm, cl, &argv[0], 1);
}

/* repeat(value, n): n=nil → coroutine, else list */
static MsValue it_repeat(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsValue val   = argv[0];
    MsValue n_val = argv[1];
    if (MS_IS_NIL(n_val)) {
        MsObjClosure* cl = get_stored_gen(vm, "_gen_repeat");
        if (!cl) { ms_vm_runtime_error(vm, "itertools.repeat: not initialized"); return MS_NIL_VAL(); }
        return make_coroutine(vm, cl, &val, 1);
    }
    if (!MS_IS_INT(n_val)) {
        ms_vm_runtime_error(vm, "itertools.repeat: n must be int or nil");
        return MS_NIL_VAL();
    }
    int n = (int)MS_AS_INT(n_val);
    if (n < 0) n = 0;
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < n; i++)
        ms_value_array_push(&out->items, val);
    return MS_OBJ_VAL(out);
}

/* ------------------------------------------------------------------ */
/* Finite combinators                                                   */
/* ------------------------------------------------------------------ */

/* chain(list, list, ...): concat all lists */
static MsValue it_chain(MsVM* vm, int argc, MsValue* argv) {
    for (int a = 0; a < argc; a++) {
        if (!MS_IS_LIST(argv[a])) {
            ms_vm_runtime_error(vm, "itertools.chain: all arguments must be lists");
            return MS_NIL_VAL();
        }
    }
    MsObjList* out = ms_obj_list_new(vm);
    for (int a = 0; a < argc; a++) {
        MsObjList* src = MS_AS_LIST(argv[a]);
        for (int i = 0; i < src->items.count; i++)
            ms_value_array_push(&out->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(out);
}

/* zip_iter(list, list, ...): zip to shortest */
static MsValue it_zip_iter(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) return MS_OBJ_VAL(ms_obj_list_new(vm));
    for (int a = 0; a < argc; a++) {
        if (!MS_IS_LIST(argv[a])) {
            ms_vm_runtime_error(vm, "itertools.zip_iter: all arguments must be lists");
            return MS_NIL_VAL();
        }
    }
    int minlen = MS_AS_LIST(argv[0])->items.count;
    for (int a = 1; a < argc; a++) {
        int l = MS_AS_LIST(argv[a])->items.count;
        if (l < minlen) minlen = l;
    }
    MsObjList* out = ms_obj_list_new(vm);
    MsValue* row_items = (MsValue*)malloc(sizeof(MsValue) * (size_t)argc);
    if (!row_items) abort();
    for (int i = 0; i < minlen; i++) {
        for (int a = 0; a < argc; a++)
            row_items[a] = MS_AS_LIST(argv[a])->items.data[i];
        MsObjList* row = ms_obj_list_from_array(vm, row_items, argc);
        ms_value_array_push(&out->items, MS_OBJ_VAL(row));
    }
    free(row_items);
    return MS_OBJ_VAL(out);
}

/* zip_longest(list, list, ..., fill): last argv = fill value, rest are lists */
static MsValue it_zip_longest(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "itertools.zip_longest: need at least (list, fill)");
        return MS_NIL_VAL();
    }
    int nlist = argc - 1;
    MsValue fill = argv[nlist];
    for (int a = 0; a < nlist; a++) {
        if (!MS_IS_LIST(argv[a])) {
            ms_vm_runtime_error(vm, "itertools.zip_longest: all list args must be lists");
            return MS_NIL_VAL();
        }
    }
    int maxlen = 0;
    for (int a = 0; a < nlist; a++) {
        int l = MS_AS_LIST(argv[a])->items.count;
        if (l > maxlen) maxlen = l;
    }
    MsObjList* out = ms_obj_list_new(vm);
    MsValue* row_items = (MsValue*)malloc(sizeof(MsValue) * (size_t)nlist);
    if (!row_items) abort();
    for (int i = 0; i < maxlen; i++) {
        for (int a = 0; a < nlist; a++) {
            MsObjList* lst = MS_AS_LIST(argv[a]);
            row_items[a] = (i < lst->items.count) ? lst->items.data[i] : fill;
        }
        MsObjList* row = ms_obj_list_from_array(vm, row_items, nlist);
        ms_value_array_push(&out->items, MS_OBJ_VAL(row));
    }
    free(row_items);
    return MS_OBJ_VAL(out);
}

/* enumerate(list, start): yields [i, v] */
static MsValue it_enumerate(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.enumerate: expected (list, int)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    ms_i64 idx = MS_AS_INT(argv[1]);
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue pair[2] = { MS_INT_VAL(idx + i), src->items.data[i] };
        MsObjList* row = ms_obj_list_from_array(vm, pair, 2);
        ms_value_array_push(&out->items, MS_OBJ_VAL(row));
    }
    return MS_OBJ_VAL(out);
}

/* flatten(list_of_iterables): one level deep */
static MsValue it_flatten(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "itertools.flatten: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue item = src->items.data[i];
        if (MS_IS_LIST(item)) {
            MsObjList* inner = MS_AS_LIST(item);
            for (int j = 0; j < inner->items.count; j++)
                ms_value_array_push(&out->items, inner->items.data[j]);
        } else {
            ms_value_array_push(&out->items, item);
        }
    }
    return MS_OBJ_VAL(out);
}

/* windowed(list, n): sliding windows of size n */
static MsValue it_windowed(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.windowed: expected (list, int)");
        return MS_NIL_VAL();
    }
    int n = (int)MS_AS_INT(argv[1]);
    if (n <= 0) {
        ms_vm_runtime_error(vm, "itertools.windowed: n must be > 0");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i + n <= src->items.count; i++) {
        MsObjList* win = ms_obj_list_new(vm);
        for (int j = 0; j < n; j++)
            ms_value_array_push(&win->items, src->items.data[i + j]);
        ms_value_array_push(&out->items, MS_OBJ_VAL(win));
    }
    return MS_OBJ_VAL(out);
}

/* batch(list, n): batches of size n */
static MsValue it_batch(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.batch: expected (list, int)");
        return MS_NIL_VAL();
    }
    int n = (int)MS_AS_INT(argv[1]);
    if (n <= 0) {
        ms_vm_runtime_error(vm, "itertools.batch: n must be > 0");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* out = ms_obj_list_new(vm);
    int i = 0;
    while (i < src->items.count) {
        MsObjList* batch = ms_obj_list_new(vm);
        for (int k = 0; k < n && i < src->items.count; k++, i++)
            ms_value_array_push(&batch->items, src->items.data[i]);
        ms_value_array_push(&out->items, MS_OBJ_VAL(batch));
    }
    return MS_OBJ_VAL(out);
}

/* accumulate(list, fn): running reduction; fn=nil → addition */
static MsValue it_accumulate(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "itertools.accumulate: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    bool use_fn = !MS_IS_NIL(fn);
    if (use_fn && !is_callable(fn)) {
        ms_vm_runtime_error(vm, "itertools.accumulate: fn must be callable or nil");
        return MS_NIL_VAL();
    }
    MsObjList* out = ms_obj_list_new(vm);
    if (src->items.count == 0) return MS_OBJ_VAL(out);
    MsValue acc = src->items.data[0];
    ms_value_array_push(&out->items, acc);
    for (int i = 1; i < src->items.count; i++) {
        if (use_fn) {
            if (!call2(vm, fn, acc, src->items.data[i], &acc)) return MS_NIL_VAL();
        } else {
            /* default: add */
            if (MS_IS_INT(acc) && MS_IS_INT(src->items.data[i]))
                acc = MS_INT_VAL(MS_AS_INT(acc) + MS_AS_INT(src->items.data[i]));
            else
                acc = MS_NUMBER_VAL(ms_as_double(acc) + ms_as_double(src->items.data[i]));
        }
        ms_value_array_push(&out->items, acc);
    }
    return MS_OBJ_VAL(out);
}

/* pairwise(list): adjacent pairs (a, b) */
static MsValue it_pairwise(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "itertools.pairwise: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i + 1 < src->items.count; i++) {
        MsValue pair[2] = { src->items.data[i], src->items.data[i + 1] };
        MsObjList* row = ms_obj_list_from_array(vm, pair, 2);
        ms_value_array_push(&out->items, MS_OBJ_VAL(row));
    }
    return MS_OBJ_VAL(out);
}

/* ------------------------------------------------------------------ */
/* Functional transforms                                                */
/* ------------------------------------------------------------------ */

/* map(fn, list): lazy map → list */
static MsValue it_map(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_callable(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.map: expected (fn, list)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[1]);
    MsValue fn = argv[0];
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        ms_value_array_push(&out->items, r);
    }
    return MS_OBJ_VAL(out);
}

/* filter(fn, list): lazy filter → list */
static MsValue it_filter(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_callable(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.filter: expected (fn, list)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[1]);
    MsValue fn = argv[0];
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue pred;
        if (!call1(vm, fn, src->items.data[i], &pred)) return MS_NIL_VAL();
        if (ms_value_is_truthy(pred))
            ms_value_array_push(&out->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(out);
}

/* starmap(fn, list_of_lists): fn(*item) for each item */
static MsValue it_starmap(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_callable(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.starmap: expected (fn, list)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[1]);
    MsValue fn = argv[0];
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue item = src->items.data[i];
        MsValue r;
        if (MS_IS_LIST(item)) {
            MsObjList* args_list = MS_AS_LIST(item);
            if (ms_vm_call_sync(vm, fn, args_list->items.data,
                                args_list->items.count, &r) != MS_INTERPRET_OK)
                return MS_NIL_VAL();
        } else {
            if (!call1(vm, fn, item, &r)) return MS_NIL_VAL();
        }
        ms_value_array_push(&out->items, r);
    }
    return MS_OBJ_VAL(out);
}

/* take(n, list): first n elements */
static MsValue it_take(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_INT(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.take: expected (int, list)");
        return MS_NIL_VAL();
    }
    int n = (int)MS_AS_INT(argv[0]);
    if (n < 0) n = 0;
    MsObjList* src = MS_AS_LIST(argv[1]);
    int lim = n < src->items.count ? n : src->items.count;
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < lim; i++)
        ms_value_array_push(&out->items, src->items.data[i]);
    return MS_OBJ_VAL(out);
}

/* take_gen(n, coroutine): materialize n items from a generator */
static MsValue it_take_gen(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_INT(argv[0]) || !MS_IS_COROUTINE(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.take_gen: expected (int, coroutine)");
        return MS_NIL_VAL();
    }
    int n = (int)MS_AS_INT(argv[0]);
    if (n < 0) n = 0;
    MsObjCoroutine* co = MS_AS_COROUTINE(argv[1]);
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < n; i++) {
        if (co->state == MS_CORO_DEAD) break; /* exhausted */
        MsValue val = MS_NIL_VAL();
        MsInterpretResult r = ms_vm_coro_resume(vm, co, MS_NIL_VAL(), &val);
        if (r != MS_INTERPRET_OK) return MS_NIL_VAL(); /* runtime error */
        if (co->state == MS_CORO_DEAD) break; /* returned (not yielded) */
        ms_value_array_push(&out->items, val);
    }
    return MS_OBJ_VAL(out);
}

/* drop(n, list): skip first n elements */
static MsValue it_drop(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_INT(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.drop: expected (int, list)");
        return MS_NIL_VAL();
    }
    int n = (int)MS_AS_INT(argv[0]);
    if (n < 0) n = 0;
    MsObjList* src = MS_AS_LIST(argv[1]);
    int start = n < src->items.count ? n : src->items.count;
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = start; i < src->items.count; i++)
        ms_value_array_push(&out->items, src->items.data[i]);
    return MS_OBJ_VAL(out);
}

/* take_while(fn, list): take while fn(x) is truthy */
static MsValue it_take_while(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_callable(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.take_while: expected (fn, list)");
        return MS_NIL_VAL();
    }
    MsValue fn = argv[0];
    MsObjList* src = MS_AS_LIST(argv[1]);
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        if (!ms_value_is_truthy(r)) break;
        ms_value_array_push(&out->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(out);
}

/* drop_while(fn, list): drop while fn(x) is truthy, then yield rest */
static MsValue it_drop_while(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_callable(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.drop_while: expected (fn, list)");
        return MS_NIL_VAL();
    }
    MsValue fn = argv[0];
    MsObjList* src = MS_AS_LIST(argv[1]);
    MsObjList* out = ms_obj_list_new(vm);
    bool dropping = true;
    for (int i = 0; i < src->items.count; i++) {
        if (dropping) {
            MsValue r;
            if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
            if (ms_value_is_truthy(r)) continue;
            dropping = false;
        }
        ms_value_array_push(&out->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(out);
}

/* reduce(fn, list, init): fold */
static MsValue it_reduce(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_callable(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.reduce: expected (fn, list, init)");
        return MS_NIL_VAL();
    }
    MsValue fn = argv[0];
    MsObjList* src = MS_AS_LIST(argv[1]);
    MsValue acc = argv[2];
    for (int i = 0; i < src->items.count; i++) {
        if (!call2(vm, fn, acc, src->items.data[i], &acc)) return MS_NIL_VAL();
    }
    return acc;
}

/* groupby(list, key): consecutive groups; yields [key, list] */
static MsValue it_groupby(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.groupby: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    MsObjList* out = ms_obj_list_new(vm);
    if (src->items.count == 0) return MS_OBJ_VAL(out);

    MsValue cur_key = MS_NIL_VAL();
    MsObjList* cur_group = NULL;
    bool first = true;

    for (int i = 0; i < src->items.count; i++) {
        MsValue k;
        if (!call1(vm, fn, src->items.data[i], &k)) return MS_NIL_VAL();
        bool same = !first && ms_value_equals(k, cur_key);
        if (!same) {
            if (cur_group) {
                MsValue kv_pair[2] = { cur_key, MS_OBJ_VAL(cur_group) };
                MsObjList* entry = ms_obj_list_from_array(vm, kv_pair, 2);
                ms_value_array_push(&out->items, MS_OBJ_VAL(entry));
            }
            cur_key = k;
            cur_group = ms_obj_list_new(vm);
            first = false;
        }
        ms_value_array_push(&cur_group->items, src->items.data[i]);
    }
    if (cur_group) {
        MsValue kv_pair[2] = { cur_key, MS_OBJ_VAL(cur_group) };
        MsObjList* entry = ms_obj_list_from_array(vm, kv_pair, 2);
        ms_value_array_push(&out->items, MS_OBJ_VAL(entry));
    }
    return MS_OBJ_VAL(out);
}

/* ------------------------------------------------------------------ */
/* Combinatoric generators                                              */
/* ------------------------------------------------------------------ */

/* product(list1, list2): Cartesian product */
static MsValue it_product(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.product: expected (list, list)");
        return MS_NIL_VAL();
    }
    MsObjList* a = MS_AS_LIST(argv[0]);
    MsObjList* b = MS_AS_LIST(argv[1]);
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < a->items.count; i++) {
        for (int j = 0; j < b->items.count; j++) {
            MsValue pair[2] = { a->items.data[i], b->items.data[j] };
            MsObjList* row = ms_obj_list_from_array(vm, pair, 2);
            ms_value_array_push(&out->items, MS_OBJ_VAL(row));
        }
    }
    return MS_OBJ_VAL(out);
}

/* Helper: generate permutations into out using Heap's algorithm */
static void gen_perms(MsVM* vm, MsValue* arr, int* idx_arr,
                      int n, int r, int depth, int* indices,
                      MsObjList* out) {
    if (depth == r) {
        MsObjList* perm = ms_obj_list_new(vm);
        for (int i = 0; i < r; i++)
            ms_value_array_push(&perm->items, arr[indices[i]]);
        ms_value_array_push(&out->items, MS_OBJ_VAL(perm));
        return;
    }
    for (int i = 0; i < n; i++) {
        bool used = false;
        for (int j = 0; j < depth; j++)
            if (indices[j] == i) { used = true; break; }
        if (!used) {
            indices[depth] = i;
            gen_perms(vm, arr, idx_arr, n, r, depth + 1, indices, out);
        }
    }
    (void)idx_arr;
}

/* permutations(list, r): r-permutations */
static MsValue it_permutations(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "itertools.permutations: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    int n = src->items.count;
    int r = n;
    if (!MS_IS_NIL(argv[1])) {
        if (!MS_IS_INT(argv[1])) {
            ms_vm_runtime_error(vm, "itertools.permutations: r must be int or nil");
            return MS_NIL_VAL();
        }
        r = (int)MS_AS_INT(argv[1]);
    }
    if (r < 0 || r > n) {
        ms_vm_runtime_error(vm, "itertools.permutations: r out of range");
        return MS_NIL_VAL();
    }
    MsObjList* out = ms_obj_list_new(vm);
    if (r == 0) return MS_OBJ_VAL(out);
    int* indices = (int*)malloc(sizeof(int) * (size_t)r);
    if (!indices) abort();
    gen_perms(vm, src->items.data, NULL, n, r, 0, indices, out);
    free(indices);
    return MS_OBJ_VAL(out);
}

/* Helper: generate combinations (no replacement) */
static void gen_combs(MsVM* vm, MsValue* arr, int n, int r,
                      int start, int depth, int* indices, MsObjList* out) {
    if (depth == r) {
        MsObjList* combo = ms_obj_list_new(vm);
        for (int i = 0; i < r; i++)
            ms_value_array_push(&combo->items, arr[indices[i]]);
        ms_value_array_push(&out->items, MS_OBJ_VAL(combo));
        return;
    }
    for (int i = start; i < n; i++) {
        indices[depth] = i;
        gen_combs(vm, arr, n, r, i + 1, depth + 1, indices, out);
    }
}

/* combinations(list, r): r-combinations without replacement */
static MsValue it_combinations(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm, "itertools.combinations: expected (list, int)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    int n = src->items.count;
    int r = (int)MS_AS_INT(argv[1]);
    if (r < 0 || r > n) {
        ms_vm_runtime_error(vm, "itertools.combinations: r out of range");
        return MS_NIL_VAL();
    }
    MsObjList* out = ms_obj_list_new(vm);
    if (r == 0) {
        MsObjList* empty = ms_obj_list_new(vm);
        ms_value_array_push(&out->items, MS_OBJ_VAL(empty));
        return MS_OBJ_VAL(out);
    }
    int* indices = (int*)malloc(sizeof(int) * (size_t)r);
    if (!indices) abort();
    gen_combs(vm, src->items.data, n, r, 0, 0, indices, out);
    free(indices);
    return MS_OBJ_VAL(out);
}

/* Helper: generate combinations with replacement */
static void gen_combs_rep(MsVM* vm, MsValue* arr, int n, int r,
                          int start, int depth, int* indices, MsObjList* out) {
    if (depth == r) {
        MsObjList* combo = ms_obj_list_new(vm);
        for (int i = 0; i < r; i++)
            ms_value_array_push(&combo->items, arr[indices[i]]);
        ms_value_array_push(&out->items, MS_OBJ_VAL(combo));
        return;
    }
    for (int i = start; i < n; i++) {
        indices[depth] = i;
        gen_combs_rep(vm, arr, n, r, i, depth + 1, indices, out);
    }
}

/* combinations_with_replacement(list, r) */
static MsValue it_combinations_with_replacement(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm,
            "itertools.combinations_with_replacement: expected (list, int)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    int n = src->items.count;
    int r = (int)MS_AS_INT(argv[1]);
    if (r < 0) {
        ms_vm_runtime_error(vm,
            "itertools.combinations_with_replacement: r must be >= 0");
        return MS_NIL_VAL();
    }
    if (n == 0) return MS_OBJ_VAL(ms_obj_list_new(vm));
    MsObjList* out = ms_obj_list_new(vm);
    if (r == 0) {
        MsObjList* empty = ms_obj_list_new(vm);
        ms_value_array_push(&out->items, MS_OBJ_VAL(empty));
        return MS_OBJ_VAL(out);
    }
    int* indices = (int*)malloc(sizeof(int) * (size_t)r);
    if (!indices) abort();
    gen_combs_rep(vm, src->items.data, n, r, 0, 0, indices, out);
    free(indices);
    return MS_OBJ_VAL(out);
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_itertools_defs[] = {
    /* infinite generators */
    {"count",    it_count,    2},
    {"cycle",    it_cycle,    1},
    {"repeat",   it_repeat,   2},
    /* finite combinators */
    {"chain",    it_chain,   -1},
    {"zip_iter", it_zip_iter,-1},
    {"zip_longest", it_zip_longest, -1},
    {"enumerate",it_enumerate,2},
    {"flatten",  it_flatten,  1},
    {"windowed", it_windowed, 2},
    {"batch",    it_batch,    2},
    {"accumulate",it_accumulate, 2},
    {"pairwise", it_pairwise, 1},
    /* functional transforms */
    {"map",       it_map,       2},
    {"filter",    it_filter,    2},
    {"starmap",   it_starmap,   2},
    {"take",      it_take,      2},
    {"take_gen",  it_take_gen,  2},
    {"drop",      it_drop,      2},
    {"take_while",it_take_while,2},
    {"drop_while",it_drop_while,2},
    {"reduce",    it_reduce,    3},
    {"groupby",   it_groupby,   2},
    /* combinatoric */
    {"product",           it_product,           2},
    {"permutations",      it_permutations,      2},
    {"combinations",      it_combinations,      2},
    {"combinations_with_replacement", it_combinations_with_replacement, 2},
    {NULL, NULL, 0}
};

/* ---- Compile one generator closure and store in module exports ---- */
static void store_gen(MsVM* vm, MsObjModule* mod,
                      const char* export_key, const char* gen_src) {
    /* Build: "fun* name(...) { ... }\nreturn name" */
    MsDiagnostic diags[4];
    int dc = 0;
    MsObjFunction* fn = ms_compile(vm, gen_src, "<itertools_init>",
                                    diags, &dc, 4);
    if (!fn) return;
    MsObjClosure* wrapper = ms_obj_closure_new(vm, fn);
    MsValue result = MS_NIL_VAL();
    if (ms_vm_call_sync(vm, MS_OBJ_VAL(wrapper), NULL, 0, &result) != MS_INTERPRET_OK)
        return;
    ms_module_export_value(vm, mod, export_key, result);
}

void ms_module_itertools_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_itertools_defs);

    /* Pre-compile generator closures used by count/cycle/repeat */
    store_gen(vm, mod, "_gen_count",
        "fun* _g(s, t) { var i = s; while (true) { yield i; i = i + t } }\n"
        "return _g");
    store_gen(vm, mod, "_gen_cycle",
        "fun* _g(lst) { var n = len(lst); var i = 0;"
        " while (true) { yield lst[i]; i = (i + 1) % n } }\n"
        "return _g");
    store_gen(vm, mod, "_gen_repeat",
        "fun* _g(v) { while (true) { yield v } }\n"
        "return _g");
}
