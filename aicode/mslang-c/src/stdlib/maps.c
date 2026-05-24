/* src/stdlib/maps.c - STDLIB-19: maps module
 *
 * Map utility functions: keys/values/items/count/clone/merge/update/
 * get/has/pop/filter/map_values/map_keys/invert/any/all/
 * from_pairs/from_keys/group_by/zip.
 *
 * All implemented in C, using ms_vm_call_sync for callback invocation.
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/vtable.h"
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static bool is_callable(MsValue v) {
    if (!MS_IS_OBJECT(v)) return false;
    MsObjectType t = MS_OBJ_TYPE(v);
    return t == MS_OBJ_CLOSURE || t == MS_OBJ_NATIVE || t == MS_OBJ_BOUND_METHOD;
}

/* Call fn(k, v) → result. Returns false on error. */
static bool call_kv(MsVM* vm, MsValue fn, MsValue k, MsValue v, MsValue* out) {
    MsValue args[2] = {k, v};
    if (ms_vm_call_sync(vm, fn, args, 2, out) != MS_INTERPRET_OK) {
        *out = MS_NIL_VAL();
        return false;
    }
    return true;
}

/* Call fn(k) → result. Returns false on error. */
static bool call_k(MsVM* vm, MsValue fn, MsValue k, MsValue* out) {
    if (ms_vm_call_sync(vm, fn, &k, 1, out) != MS_INTERPRET_OK) {
        *out = MS_NIL_VAL();
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Extraction                                                           */
/* ------------------------------------------------------------------ */

static MsValue mp_keys(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.keys: expected map");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        ms_value_array_push(&out->items, e->key);
    }
    return MS_OBJ_VAL(out);
}

static MsValue mp_values(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.values: expected map");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        ms_value_array_push(&out->items, e->value);
    }
    return MS_OBJ_VAL(out);
}

static MsValue mp_items(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.items: expected map");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsObjList* out = ms_obj_list_new(vm);
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        MsValue pair[2] = {e->key, e->value};
        MsObjList* row = ms_obj_list_from_array(vm, pair, 2);
        ms_value_array_push(&out->items, MS_OBJ_VAL(row));
    }
    return MS_OBJ_VAL(out);
}

static MsValue mp_count(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.count: expected map");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    int n = 0;
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (e->used && !e->tombstone) n++;
    }
    return MS_INT_VAL((ms_i64)n);
}

/* ------------------------------------------------------------------ */
/* Copy & Merge                                                         */
/* ------------------------------------------------------------------ */

static MsValue mp_clone(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.clone: expected map");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        ms_vtable_set(&out->table, e->key, e->value);
    }
    return MS_OBJ_VAL(out);
}

/* merge(m1, m2, ...) - variadic, later keys overwrite earlier */
static MsValue mp_merge(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) {
        ms_vm_runtime_error(vm, "maps.merge: expected at least one map");
        return MS_NIL_VAL();
    }
    MsObjMap* out = ms_obj_map_new(vm);
    for (int a = 0; a < argc; a++) {
        if (!MS_IS_MAP(argv[a])) {
            ms_vm_runtime_error(vm, "maps.merge: all arguments must be maps");
            return MS_NIL_VAL();
        }
        MsValueTable* t = &MS_AS_MAP(argv[a])->table;
        for (int i = 0; i < t->capacity; i++) {
            MsVEntry* e = &t->entries[i];
            if (!e->used || e->tombstone) continue;
            ms_vtable_set(&out->table, e->key, e->value);
        }
    }
    return MS_OBJ_VAL(out);
}

/* update(m, key, fn) - in-place: m[key] = fn(m[key]) */
static MsValue mp_update(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0]) || !is_callable(argv[2])) {
        ms_vm_runtime_error(vm, "maps.update: expected (map, key, fn)");
        return MS_NIL_VAL();
    }
    MsObjMap* m = MS_AS_MAP(argv[0]);
    MsValue key = argv[1];
    MsValue fn  = argv[2];
    MsValue old;
    if (!ms_vtable_get(&m->table, key, &old)) old = MS_NIL_VAL();
    MsValue newval;
    MsValue arg = old;
    if (ms_vm_call_sync(vm, fn, &arg, 1, &newval) != MS_INTERPRET_OK)
        return MS_NIL_VAL();
    ms_vtable_set(&m->table, key, newval);
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* Query                                                                */
/* ------------------------------------------------------------------ */

static MsValue mp_get(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.get: expected map");
        return MS_NIL_VAL();
    }
    MsValue def = (argc >= 3) ? argv[2] : MS_NIL_VAL();
    MsValue out;
    if (ms_vtable_get(&MS_AS_MAP(argv[0])->table, argv[1], &out))
        return out;
    return def;
}

static MsValue mp_has(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.has: expected map");
        return MS_NIL_VAL();
    }
    MsValue dummy;
    return MS_BOOL_VAL(ms_vtable_get(&MS_AS_MAP(argv[0])->table, argv[1], &dummy));
}

static MsValue mp_pop(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.pop: expected map");
        return MS_NIL_VAL();
    }
    MsValue def = (argc >= 3) ? argv[2] : MS_NIL_VAL();
    MsObjMap* m = MS_AS_MAP(argv[0]);
    MsValue out;
    if (ms_vtable_get(&m->table, argv[1], &out)) {
        ms_vtable_delete(&m->table, argv[1]);
        return out;
    }
    return def;
}

/* ------------------------------------------------------------------ */
/* Transform & Filter                                                   */
/* ------------------------------------------------------------------ */

static MsValue mp_filter(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "maps.filter: expected (map, fn)");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsValue fn = argv[1];
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        MsValue pred;
        if (!call_kv(vm, fn, e->key, e->value, &pred)) return MS_NIL_VAL();
        if (ms_value_is_truthy(pred))
            ms_vtable_set(&out->table, e->key, e->value);
    }
    return MS_OBJ_VAL(out);
}

static MsValue mp_map_values(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "maps.map_values: expected (map, fn)");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsValue fn = argv[1];
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        MsValue newval;
        if (!call_kv(vm, fn, e->key, e->value, &newval)) return MS_NIL_VAL();
        ms_vtable_set(&out->table, e->key, newval);
    }
    return MS_OBJ_VAL(out);
}

static MsValue mp_map_keys(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "maps.map_keys: expected (map, fn)");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsValue fn = argv[1];
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        MsValue newkey;
        if (!call_k(vm, fn, e->key, &newkey)) return MS_NIL_VAL();
        ms_vtable_set(&out->table, newkey, e->value);
    }
    return MS_OBJ_VAL(out);
}

static MsValue mp_invert(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0])) {
        ms_vm_runtime_error(vm, "maps.invert: expected map");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        ms_vtable_set(&out->table, e->value, e->key);
    }
    return MS_OBJ_VAL(out);
}

static MsValue mp_any(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "maps.any: expected (map, fn)");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsValue fn = argv[1];
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        MsValue r;
        if (!call_kv(vm, fn, e->key, e->value, &r)) return MS_NIL_VAL();
        if (ms_value_is_truthy(r)) return MS_BOOL_VAL(true);
    }
    return MS_BOOL_VAL(false);
}

static MsValue mp_all(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_MAP(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "maps.all: expected (map, fn)");
        return MS_NIL_VAL();
    }
    MsValueTable* t = &MS_AS_MAP(argv[0])->table;
    MsValue fn = argv[1];
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        MsValue r;
        if (!call_kv(vm, fn, e->key, e->value, &r)) return MS_NIL_VAL();
        if (!ms_value_is_truthy(r)) return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

/* ------------------------------------------------------------------ */
/* Construction                                                         */
/* ------------------------------------------------------------------ */

static MsValue mp_from_pairs(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "maps.from_pairs: expected list of [k,v] pairs");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue pair = src->items.data[i];
        if (!MS_IS_LIST(pair) || MS_AS_LIST(pair)->items.count < 2) {
            ms_vm_runtime_error(vm, "maps.from_pairs: each pair must be a list [k, v]");
            return MS_NIL_VAL();
        }
        MsObjList* p = MS_AS_LIST(pair);
        ms_vtable_set(&out->table, p->items.data[0], p->items.data[1]);
    }
    return MS_OBJ_VAL(out);
}

static MsValue mp_from_keys(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "maps.from_keys: expected list");
        return MS_NIL_VAL();
    }
    MsValue def = (argc >= 2) ? argv[1] : MS_NIL_VAL();
    MsObjList* keys = MS_AS_LIST(argv[0]);
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < keys->items.count; i++)
        ms_vtable_set(&out->table, keys->items.data[i], def);
    return MS_OBJ_VAL(out);
}

static MsValue mp_group_by(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !is_callable(argv[1])) {
        ms_vm_runtime_error(vm, "maps.group_by: expected (list, fn)");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue fn = argv[1];
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < src->items.count; i++) {
        MsValue key;
        if (!call_k(vm, fn, src->items.data[i], &key)) return MS_NIL_VAL();
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

/* zip(keys_list, values_list) */
static MsValue mp_zip(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "maps.zip: expected (list, list)");
        return MS_NIL_VAL();
    }
    MsObjList* keys = MS_AS_LIST(argv[0]);
    MsObjList* vals = MS_AS_LIST(argv[1]);
    int n = keys->items.count < vals->items.count
          ? keys->items.count : vals->items.count;
    MsObjMap* out = ms_obj_map_new(vm);
    for (int i = 0; i < n; i++)
        ms_vtable_set(&out->table, keys->items.data[i], vals->items.data[i]);
    return MS_OBJ_VAL(out);
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_maps_defs[] = {
    /* extraction */
    {"keys",       mp_keys,       1},
    {"values",     mp_values,     1},
    {"items",      mp_items,      1},
    {"count",      mp_count,      1},
    /* copy & merge */
    {"clone",      mp_clone,      1},
    {"merge",      mp_merge,     -1},
    {"update",     mp_update,     3},
    /* query */
    {"get",        mp_get,       -1},
    {"has",        mp_has,        2},
    {"pop",        mp_pop,       -1},
    /* transform & filter */
    {"filter",     mp_filter,     2},
    {"map_values", mp_map_values, 2},
    {"map_keys",   mp_map_keys,   2},
    {"invert",     mp_invert,     1},
    {"any",        mp_any,        2},
    {"all",        mp_all,        2},
    /* construction */
    {"from_pairs", mp_from_pairs, 1},
    {"from_keys",  mp_from_keys, -1},
    {"group_by",   mp_group_by,   2},
    {"zip",        mp_zip,        2},
    {NULL, NULL, 0}
};

void ms_module_maps_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_maps_defs);
}
