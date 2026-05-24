/* src/stdlib/slices.c - STDLIB-18: slices module
 *
 * Functional list utilities: map/filter/reduce/any/all/count/group_by/partition,
 * find/find_index/index_of/contains/last_index_of, reverse/flatten/unique/
 * chunk/zip/enumerate/concat/take/drop/take_while/drop_while,
 * max/min/sum/sorted/is_sorted.
 *
 * All implemented in C, using ms_vm_call_sync for callback invocation.
 * sorted/is_sorted use a simple insertion sort (STDLIB-20 not yet available).
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/vtable.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* Call fn(v) → result. Returns false and sets *out=NIL on error. */
static bool call1(MsVM* vm, MsValue fn, MsValue v, MsValue* out) {
    MsValue arg = v;
    if (ms_vm_call_sync(vm, fn, &arg, 1, out) != MS_INTERPRET_OK) {
        *out = MS_NIL_VAL();
        return false;
    }
    return true;
}

/* Call fn(acc, v) → result. */
static bool call2(MsVM* vm, MsValue fn, MsValue a, MsValue b, MsValue* out) {
    MsValue args[2] = {a, b};
    if (ms_vm_call_sync(vm, fn, args, 2, out) != MS_INTERPRET_OK) {
        *out = MS_NIL_VAL();
        return false;
    }
    return true;
}

static bool is_callable(MsValue v) {
    if (!MS_IS_OBJECT(v)) return false;
    MsObjectType t = MS_OBJ_TYPE(v);
    return t == MS_OBJ_CLOSURE || t == MS_OBJ_NATIVE || t == MS_OBJ_BOUND_METHOD;
}

/* ------------------------------------------------------------------ */
/* Functional operations                                               */
/* ------------------------------------------------------------------ */

static MsValue sl_map(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.map: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* dst = ms_obj_list_new(vm);
    MsValue fn = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        MsValue result;
        if (!call1(vm, fn, src->items.data[i], &result)) return MS_NIL_VAL();
        ms_value_array_push(&dst->items, result);
    }
    return MS_OBJ_VAL(dst);
}

static MsValue sl_filter(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.filter: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* dst = ms_obj_list_new(vm);
    MsValue fn = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        MsValue pred;
        if (!call1(vm, fn, src->items.data[i], &pred)) return MS_NIL_VAL();
        if (ms_value_is_truthy(pred))
            ms_value_array_push(&dst->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(dst);
}

static MsValue sl_flat_map(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.flat_map: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* dst = ms_obj_list_new(vm);
    MsValue fn = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        MsValue sub;
        if (!call1(vm, fn, src->items.data[i], &sub)) return MS_NIL_VAL();
        if (MS_IS_LIST(sub)) {
            MsObjList* inner = MS_AS_LIST(sub);
            for (int j = 0; j < inner->items.count; j++)
                ms_value_array_push(&dst->items, inner->items.data[j]);
        } else {
            ms_value_array_push(&dst->items, sub);
        }
    }
    return MS_OBJ_VAL(dst);
}

static MsValue sl_reduce(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.reduce: expected (list, fn, init)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    MsValue acc = argv[2];
    for (int i = 0; i < src->items.count; i++) {
        if (!call2(vm, fn, acc, src->items.data[i], &acc)) return MS_NIL_VAL();
    }
    return acc;
}

static MsValue sl_any(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.any: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        if (ms_value_is_truthy(r)) return MS_BOOL_VAL(true);
    }
    return MS_BOOL_VAL(false);
}

static MsValue sl_all(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.all: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        if (!ms_value_is_truthy(r)) return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

static MsValue sl_count(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.count: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    ms_i64 n = 0;
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        if (ms_value_is_truthy(r)) n++;
    }
    return MS_INT_VAL(n);
}

static MsValue sl_group_by(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.group_by: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjMap* out = ms_obj_map_new(vm);
    MsValue fn = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        MsValue key;
        if (!call1(vm, fn, src->items.data[i], &key)) return MS_NIL_VAL();
        MsValue existing;
        MsObjList* bucket;
        if (ms_vtable_get(&out->table, key, &existing) && MS_IS_LIST(existing)) {
            bucket = MS_AS_LIST(existing);
        } else {
            bucket = ms_obj_list_new(vm);
            ms_vtable_set(&out->table, key, MS_OBJ_VAL(bucket));
        }
        ms_value_array_push(&bucket->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(out);
}

static MsValue sl_partition(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.partition: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    MsObjList* yes = ms_obj_list_new(vm);
    MsObjList* no  = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        if (ms_value_is_truthy(r))
            ms_value_array_push(&yes->items, src->items.data[i]);
        else
            ms_value_array_push(&no->items, src->items.data[i]);
    }
    MsValue pair[2] = { MS_OBJ_VAL(yes), MS_OBJ_VAL(no) };
    return MS_OBJ_VAL(ms_obj_list_from_array(vm, pair, 2));
}

/* ------------------------------------------------------------------ */
/* Find & index                                                         */
/* ------------------------------------------------------------------ */

static MsValue sl_find(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.find: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        if (ms_value_is_truthy(r)) return src->items.data[i];
    }
    return MS_NIL_VAL();
}

static MsValue sl_find_index(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.find_index: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        if (ms_value_is_truthy(r)) return MS_INT_VAL((ms_i64)i);
    }
    return MS_INT_VAL(-1);
}

static MsValue sl_index_of(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.index_of: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue target = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        if (ms_value_equals(src->items.data[i], target))
            return MS_INT_VAL((ms_i64)i);
    }
    return MS_INT_VAL(-1);
}

static MsValue sl_contains(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.contains: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue target = argv[1];
    for (int i = 0; i < src->items.count; i++) {
        if (ms_value_equals(src->items.data[i], target))
            return MS_BOOL_VAL(true);
    }
    return MS_BOOL_VAL(false);
}

static MsValue sl_last_index_of(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.last_index_of: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue target = argv[1];
    for (int i = src->items.count - 1; i >= 0; i--) {
        if (ms_value_equals(src->items.data[i], target))
            return MS_INT_VAL((ms_i64)i);
    }
    return MS_INT_VAL(-1);
}

/* ------------------------------------------------------------------ */
/* Transformations                                                      */
/* ------------------------------------------------------------------ */

static MsValue sl_reverse(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.reverse: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* dst = ms_obj_list_new(vm);
    for (int i = src->items.count - 1; i >= 0; i--)
        ms_value_array_push(&dst->items, src->items.data[i]);
    return MS_OBJ_VAL(dst);
}

static MsValue sl_reverse_in_place(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.reverse_in_place: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    int lo = 0, hi = src->items.count - 1;
    while (lo < hi) {
        MsValue tmp = src->items.data[lo];
        src->items.data[lo] = src->items.data[hi];
        src->items.data[hi] = tmp;
        lo++; hi--;
    }
    return MS_NIL_VAL();
}

static MsValue sl_flatten(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.flatten: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* dst = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue item = src->items.data[i];
        if (MS_IS_LIST(item)) {
            MsObjList* inner = MS_AS_LIST(item);
            for (int j = 0; j < inner->items.count; j++)
                ms_value_array_push(&dst->items, inner->items.data[j]);
        } else {
            ms_value_array_push(&dst->items, item);
        }
    }
    return MS_OBJ_VAL(dst);
}

static MsValue sl_unique(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.unique: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* dst = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        bool found = false;
        for (int j = 0; j < dst->items.count; j++) {
            if (ms_value_equals(src->items.data[i], dst->items.data[j])) {
                found = true;
                break;
            }
        }
        if (!found)
            ms_value_array_push(&dst->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(dst);
}

static MsValue sl_chunk(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm, "slices.chunk: expected (list, int)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    ms_i64 n = MS_AS_INT(argv[1]);
    if (n <= 0) {
        ms_vm_runtime_error(vm, "slices.chunk: n must be > 0");
        return MS_NIL_VAL();
    }
    MsObjList* out = ms_obj_list_new(vm);
    int i = 0;
    while (i < src->items.count) {
        MsObjList* chunk = ms_obj_list_new(vm);
        for (ms_i64 k = 0; k < n && i < src->items.count; k++, i++)
            ms_value_array_push(&chunk->items, src->items.data[i]);
        ms_value_array_push(&out->items, MS_OBJ_VAL(chunk));
    }
    return MS_OBJ_VAL(out);
}

/* zip(list1, list2, ...) - variadic */
static MsValue sl_zip(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) {
        ms_vm_runtime_error(vm, "slices.zip: expected at least one list");
        return MS_NIL_VAL();
    }
    for (int a = 0; a < argc; a++) {
        if (!MS_IS_LIST(argv[a])) {
            ms_vm_runtime_error(vm, "slices.zip: all arguments must be lists");
            return MS_NIL_VAL();
        }
    }
    /* Minimum length across all lists */
    int minlen = MS_AS_LIST(argv[0])->items.count;
    for (int a = 1; a < argc; a++) {
        int l = MS_AS_LIST(argv[a])->items.count;
        if (l < minlen) minlen = l;
    }
    MsObjList* out = ms_obj_list_new(vm);
    MsValue* tuple_items = (MsValue*)malloc(sizeof(MsValue) * (size_t)argc);
    if (!tuple_items) abort();
    for (int i = 0; i < minlen; i++) {
        for (int a = 0; a < argc; a++)
            tuple_items[a] = MS_AS_LIST(argv[a])->items.data[i];
        MsObjList* row = ms_obj_list_from_array(vm, tuple_items, argc);
        ms_value_array_push(&out->items, MS_OBJ_VAL(row));
    }
    free(tuple_items);
    return MS_OBJ_VAL(out);
}

static MsValue sl_enumerate(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.enumerate: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue pair[2] = { MS_INT_VAL((ms_i64)i), src->items.data[i] };
        MsObjList* row = ms_obj_list_from_array(vm, pair, 2);
        ms_value_array_push(&out->items, MS_OBJ_VAL(row));
    }
    return MS_OBJ_VAL(out);
}

/* concat(list1, list2, ...) - variadic */
static MsValue sl_concat(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) {
        ms_vm_runtime_error(vm, "slices.concat: expected at least one list");
        return MS_NIL_VAL();
    }
    MsObjList* out = ms_obj_list_new(vm);
    for (int a = 0; a < argc; a++) {
        if (!MS_IS_LIST(argv[a])) {
            ms_vm_runtime_error(vm, "slices.concat: all arguments must be lists");
            return MS_NIL_VAL();
        }
        MsObjList* src = MS_AS_LIST(argv[a]);
        for (int i = 0; i < src->items.count; i++)
            ms_value_array_push(&out->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(out);
}

static MsValue sl_take(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm, "slices.take: expected (list, int)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    ms_i64 n = MS_AS_INT(argv[1]);
    if (n < 0) n = 0;
    MsObjList* out = ms_obj_list_new(vm);
    int lim = (int)n < src->items.count ? (int)n : src->items.count;
    for (int i = 0; i < lim; i++)
        ms_value_array_push(&out->items, src->items.data[i]);
    return MS_OBJ_VAL(out);
}

static MsValue sl_drop(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm, "slices.drop: expected (list, int)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    ms_i64 n = MS_AS_INT(argv[1]);
    if (n < 0) n = 0;
    int start = (int)n < src->items.count ? (int)n : src->items.count;
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = start; i < src->items.count; i++)
        ms_value_array_push(&out->items, src->items.data[i]);
    return MS_OBJ_VAL(out);
}

static MsValue sl_take_while(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.take_while: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue r;
        if (!call1(vm, fn, src->items.data[i], &r)) return MS_NIL_VAL();
        if (!ms_value_is_truthy(r)) break;
        ms_value_array_push(&out->items, src->items.data[i]);
    }
    return MS_OBJ_VAL(out);
}

static MsValue sl_drop_while(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "slices.drop_while: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
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

/* ------------------------------------------------------------------ */
/* Stats & sort                                                         */
/* ------------------------------------------------------------------ */

/* Compare two values numerically (for max/min/sum). */
static double to_num(MsValue v) {
    if (MS_IS_INT(v)) return (double)MS_AS_INT(v);
    if (MS_IS_NUMBER(v)) return MS_AS_NUMBER(v);
    return 0.0;
}

static MsValue sl_max(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.max: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    if (src->items.count == 0) return MS_NIL_VAL();
    MsValue fn = (argc >= 2 && is_callable(argv[1])) ? argv[1] : MS_NIL_VAL();
    bool has_key = !MS_IS_NIL(fn);
    MsValue best = src->items.data[0];
    MsValue best_key;
    if (has_key) {
        if (!call1(vm, fn, best, &best_key)) return MS_NIL_VAL();
    } else {
        best_key = best;
    }
    for (int i = 1; i < src->items.count; i++) {
        MsValue item = src->items.data[i];
        MsValue item_key;
        if (has_key) {
            if (!call1(vm, fn, item, &item_key)) return MS_NIL_VAL();
        } else {
            item_key = item;
        }
        if (to_num(item_key) > to_num(best_key)) {
            best = item;
            best_key = item_key;
        }
    }
    return best;
}

static MsValue sl_min(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.min: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    if (src->items.count == 0) return MS_NIL_VAL();
    MsValue fn = (argc >= 2 && is_callable(argv[1])) ? argv[1] : MS_NIL_VAL();
    bool has_key = !MS_IS_NIL(fn);
    MsValue best = src->items.data[0];
    MsValue best_key;
    if (has_key) {
        if (!call1(vm, fn, best, &best_key)) return MS_NIL_VAL();
    } else {
        best_key = best;
    }
    for (int i = 1; i < src->items.count; i++) {
        MsValue item = src->items.data[i];
        MsValue item_key;
        if (has_key) {
            if (!call1(vm, fn, item, &item_key)) return MS_NIL_VAL();
        } else {
            item_key = item;
        }
        if (to_num(item_key) < to_num(best_key)) {
            best = item;
            best_key = item_key;
        }
    }
    return best;
}

static MsValue sl_sum(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.sum: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    bool all_int = true;
    ms_i64 isum = 0;
    double dsum = 0.0;
    for (int i = 0; i < src->items.count; i++) {
        MsValue v = src->items.data[i];
        if (MS_IS_INT(v)) {
            isum += MS_AS_INT(v);
            dsum += (double)MS_AS_INT(v);
        } else if (MS_IS_NUMBER(v)) {
            dsum += MS_AS_NUMBER(v);
            all_int = false;
        } else {
            ms_vm_runtime_error(vm, "slices.sum: non-numeric element");
            return MS_NIL_VAL();
        }
    }
    return all_int ? MS_INT_VAL(isum) : MS_NUMBER_VAL(dsum);
}

/* Insertion sort (stable, O(n^2) - STDLIB-20 sort module not yet available) */
typedef struct {
    MsValue item;
    MsValue key;
} SortEntry;

static MsValue sl_sorted(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.sorted: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    int n = src->items.count;
    if (n == 0) return MS_OBJ_VAL(ms_obj_list_new(vm));

    MsValue fn = (argc >= 2 && is_callable(argv[1])) ? argv[1] : MS_NIL_VAL();
    bool has_key = !MS_IS_NIL(fn);
    bool rev = (argc >= 3 && MS_IS_BOOL(argv[2]) && MS_AS_BOOL(argv[2]));

    SortEntry* arr = (SortEntry*)malloc(sizeof(SortEntry) * (size_t)n);
    if (!arr) abort();

    for (int i = 0; i < n; i++) {
        arr[i].item = src->items.data[i];
        if (has_key) {
            if (!call1(vm, fn, arr[i].item, &arr[i].key)) {
                free(arr);
                return MS_NIL_VAL();
            }
        } else {
            arr[i].key = arr[i].item;
        }
    }

    /* Stable insertion sort */
    for (int i = 1; i < n; i++) {
        SortEntry cur = arr[i];
        double cur_k = to_num(cur.key);
        int j = i - 1;
        while (j >= 0) {
            double k = to_num(arr[j].key);
            bool should_shift = rev ? (k < cur_k) : (k > cur_k);
            if (!should_shift) break;
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = cur;
    }

    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < n; i++)
        ms_value_array_push(&out->items, arr[i].item);
    free(arr);
    return MS_OBJ_VAL(out);
}

static MsValue sl_is_sorted(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "slices.is_sorted: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    if (src->items.count <= 1) return MS_BOOL_VAL(true);

    MsValue fn = (argc >= 2 && is_callable(argv[1])) ? argv[1] : MS_NIL_VAL();
    bool has_key = !MS_IS_NIL(fn);

    for (int i = 1; i < src->items.count; i++) {
        MsValue ka, kb;
        if (has_key) {
            if (!call1(vm, fn, src->items.data[i - 1], &ka)) return MS_NIL_VAL();
            if (!call1(vm, fn, src->items.data[i], &kb)) return MS_NIL_VAL();
        } else {
            ka = src->items.data[i - 1];
            kb = src->items.data[i];
        }
        if (to_num(ka) > to_num(kb)) return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_slices_defs[] = {
    /* functional */
    {"map",         sl_map,              2},
    {"filter",      sl_filter,           2},
    {"flat_map",    sl_flat_map,         2},
    {"reduce",      sl_reduce,           3},
    {"any",         sl_any,              2},
    {"all",         sl_all,              2},
    {"count",       sl_count,            2},
    {"group_by",    sl_group_by,         2},
    {"partition",   sl_partition,        2},
    /* find */
    {"find",        sl_find,             2},
    {"find_index",  sl_find_index,       2},
    {"index_of",    sl_index_of,         2},
    {"contains",    sl_contains,         2},
    {"last_index_of", sl_last_index_of,  2},
    /* transform */
    {"reverse",        sl_reverse,          1},
    {"reverse_in_place", sl_reverse_in_place, 1},
    {"flatten",        sl_flatten,          1},
    {"unique",         sl_unique,           1},
    {"chunk",          sl_chunk,            2},
    {"zip",            sl_zip,             -1},
    {"enumerate",      sl_enumerate,        1},
    {"concat",         sl_concat,          -1},
    {"take",           sl_take,             2},
    {"drop",           sl_drop,             2},
    {"take_while",     sl_take_while,       2},
    {"drop_while",     sl_drop_while,       2},
    /* stats & sort */
    {"max",         sl_max,             -1},
    {"min",         sl_min,             -1},
    {"sum",         sl_sum,              1},
    {"sorted",      sl_sorted,          -1},
    {"is_sorted",   sl_is_sorted,       -1},
    {NULL, NULL, 0}
};

void ms_module_slices_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_slices_defs);
}
