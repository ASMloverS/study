/* src/stdlib/set.c - STDLIB-21: set module
 *
 * Set data structure backed by ObjMap (key presence = membership).
 * Returns Set instances whose methods are registered as native class methods.
 *
 * Public API (all return new Set unless noted):
 *   set.new(iterable=nil)      → Set
 *   set.from_list(list)        → Set
 *
 * Instance methods:
 *   s.add(value)               → nil
 *   s.remove(value)            → nil   (silent if absent)
 *   s.discard(value)           → nil   (alias for remove)
 *   s.contains(value)          → bool
 *   s.size()                   → int
 *   s.is_empty()               → bool
 *   s.to_list()                → list
 *   s.clear()                  → nil
 *   s.union(other)             → Set
 *   s.intersection(other)      → Set
 *   s.difference(other)        → Set
 *   s.symmetric_difference(other) → Set
 *   s.copy()                   → Set
 *   s.is_subset(other)         → bool
 *   s.is_superset(other)       → bool
 *   s.is_disjoint(other)       → bool
 *   s.equals(other)            → bool
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/vtable.h"
#include "ms/shape.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/* Intern a C string into the VM string pool. */
static MsObjString* intern(MsVM* vm, const char* s) {
    return ms_obj_string_copy(vm, s, (int)strlen(s));
}

/* Get the Set class from the module cache.
   The cache key for builtins is "<builtin:set>". */
static MsObjClass* get_set_class(MsVM* vm) {
    MsObjString* key = intern(vm, "<builtin:set>");
    MsValue mod_val;
    if (!ms_table_get(&vm->module_cache, key, &mod_val)) return NULL;
    if (!MS_IS_MODULE(mod_val)) return NULL;
    MsObjModule* mod = MS_AS_MODULE(mod_val);
    MsObjString* ckey = intern(vm, "Set");
    MsValue cv;
    if (!ms_table_get(&mod->exports, ckey, &cv)) return NULL;
    if (!MS_IS_CLASS(cv)) return NULL;
    return MS_AS_CLASS(cv);
}

/* ------------------------------------------------------------------ */
/* Shape-based field access (_data field holds the backing ObjMap)     */
/* ------------------------------------------------------------------ */

static void inst_set_field(MsVM* vm, MsObjInstance* inst,
                           const char* name, MsValue val) {
    MsObjString* key = intern(vm, name);
    int slot = ms_shape_find_slot(inst->shape, key);
    if (slot < 0) {
        inst->shape = ms_shape_transition(vm, inst->shape, key);
        slot = ms_shape_find_slot(inst->shape, key);
    }
    if (slot >= MS_SBO_FIELDS && !inst->overflow_fields) {
        int extra = slot - MS_SBO_FIELDS + 1;
        inst->overflow_fields =
            (MsValue*)calloc((size_t)extra, sizeof(MsValue));
    }
    *ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot) = val;
}

static MsValue inst_get_field(MsObjInstance* inst, const char* name) {
    for (int i = 0; i < inst->shape->slots.count; i++) {
        MsSmallEntry* e = &inst->shape->slots.data[i];
        if (e->key && strcmp(e->key->data, name) == 0) {
            return *ms_shape_field_ptr(inst->inline_fields,
                                       inst->overflow_fields,
                                       (int)e->value);
        }
    }
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* Type guard and backing-map extraction                               */
/* ------------------------------------------------------------------ */

static bool is_set_inst(MsValue v) {
    if (!MS_IS_INSTANCE(v)) return false;
    return strcmp(MS_AS_INSTANCE(v)->klass->name->data, "Set") == 0;
}

/* Get the backing ObjMap from a Set instance. Returns NULL on type error. */
static MsObjMap* set_data(MsValue v) {
    if (!is_set_inst(v)) return NULL;
    MsValue d = inst_get_field(MS_AS_INSTANCE(v), "_data");
    if (!MS_IS_MAP(d)) return NULL;
    return MS_AS_MAP(d);
}

/* ------------------------------------------------------------------ */
/* Set construction helpers                                            */
/* ------------------------------------------------------------------ */

/* Create a new Set instance with an empty backing map. */
static MsValue make_set(MsVM* vm, MsObjClass* klass) {
    MsObjInstance* inst = ms_obj_instance_new(vm, klass);
    MsObjMap* map = ms_obj_map_new(vm);
    inst_set_field(vm, inst, "_data", MS_OBJ_VAL(map));
    return MS_OBJ_VAL(inst);
}

/* Populate a Set instance from a list. */
static bool populate_from_list(MsObjMap* m, MsObjList* lst) {
    MsValue t = MS_BOOL_VAL(true);
    for (int i = 0; i < lst->items.count; i++)
        ms_vtable_set(&m->table, lst->items.data[i], t);
    return true;
}

/* ------------------------------------------------------------------ */
/* Module-level constructors                                           */
/* ------------------------------------------------------------------ */

static MsValue set_new(MsVM* vm, int argc, MsValue* argv) {
    MsObjClass* klass = get_set_class(vm);
    if (!klass) {
        ms_vm_runtime_error(vm, "set.new: Set class not found");
        return MS_NIL_VAL();
    }
    /* Optional first argument: iterable list */
    if (argc >= 1 && !MS_IS_NIL(argv[0])) {
        if (!MS_IS_LIST(argv[0])) {
            ms_vm_runtime_error(vm, "set.new: argument must be a list or nil");
            return MS_NIL_VAL();
        }
        MsValue s = make_set(vm, klass);
        populate_from_list(MS_AS_MAP(inst_get_field(MS_AS_INSTANCE(s), "_data")),
                           MS_AS_LIST(argv[0]));
        return s;
    }
    return make_set(vm, klass);
}

static MsValue set_from_list(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "set.from_list: expected list");
        return MS_NIL_VAL();
    }
    MsObjClass* klass = get_set_class(vm);
    if (!klass) {
        ms_vm_runtime_error(vm, "set.from_list: Set class not found");
        return MS_NIL_VAL();
    }
    MsValue s = make_set(vm, klass);
    populate_from_list(MS_AS_MAP(inst_get_field(MS_AS_INSTANCE(s), "_data")),
                       MS_AS_LIST(argv[0]));
    return s;
}

/* ------------------------------------------------------------------ */
/* Instance methods (argv[0] = self)                                   */
/* ------------------------------------------------------------------ */

static MsValue set_add(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* m = set_data(argv[0]);
    if (!m) {
        ms_vm_runtime_error(vm, "set.add: receiver is not a Set");
        return MS_NIL_VAL();
    }
    ms_vtable_set(&m->table, argv[1], MS_BOOL_VAL(true));
    return MS_NIL_VAL();
}

static MsValue set_remove(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* m = set_data(argv[0]);
    if (!m) {
        ms_vm_runtime_error(vm, "set.remove: receiver is not a Set");
        return MS_NIL_VAL();
    }
    ms_vtable_delete(&m->table, argv[1]);
    return MS_NIL_VAL();
}

/* discard is the same as remove */
static MsValue set_discard(MsVM* vm, int argc, MsValue* argv) {
    return set_remove(vm, argc, argv);
}

static MsValue set_contains(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* m = set_data(argv[0]);
    if (!m) {
        ms_vm_runtime_error(vm, "set.contains: receiver is not a Set");
        return MS_NIL_VAL();
    }
    MsValue dummy;
    return MS_BOOL_VAL(ms_vtable_get(&m->table, argv[1], &dummy));
}

static MsValue set_size(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* m = set_data(argv[0]);
    if (!m) {
        ms_vm_runtime_error(vm, "set.size: receiver is not a Set");
        return MS_NIL_VAL();
    }
    int n = 0;
    for (int i = 0; i < m->table.capacity; i++) {
        MsVEntry* e = &m->table.entries[i];
        if (e->used && !e->tombstone) n++;
    }
    return MS_INT_VAL((ms_i64)n);
}

static MsValue set_is_empty(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* m = set_data(argv[0]);
    if (!m) {
        ms_vm_runtime_error(vm, "set.is_empty: receiver is not a Set");
        return MS_NIL_VAL();
    }
    for (int i = 0; i < m->table.capacity; i++) {
        MsVEntry* e = &m->table.entries[i];
        if (e->used && !e->tombstone) return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

static MsValue set_to_list(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* m = set_data(argv[0]);
    if (!m) {
        ms_vm_runtime_error(vm, "set.to_list: receiver is not a Set");
        return MS_NIL_VAL();
    }
    MsObjList* lst = ms_obj_list_new(vm);
    for (int i = 0; i < m->table.capacity; i++) {
        MsVEntry* e = &m->table.entries[i];
        if (e->used && !e->tombstone)
            ms_value_array_push(&lst->items, e->key);
    }
    return MS_OBJ_VAL(lst);
}

static MsValue set_clear(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* m = set_data(argv[0]);
    if (!m) {
        ms_vm_runtime_error(vm, "set.clear: receiver is not a Set");
        return MS_NIL_VAL();
    }
    /* Replace the backing map entirely — simplest approach. */
    MsObjMap* fresh = ms_obj_map_new(vm);
    inst_set_field(vm, MS_AS_INSTANCE(argv[0]), "_data", MS_OBJ_VAL(fresh));
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* Set operations (return new Set)                                     */
/* ------------------------------------------------------------------ */

/* Helper: create a new Set from an existing ObjMap (copies entries). */
static MsValue set_from_map(MsVM* vm, MsValueTable* src_table) {
    MsObjClass* klass = get_set_class(vm);
    if (!klass) {
        ms_vm_runtime_error(vm, "set: Set class not found");
        return MS_NIL_VAL();
    }
    MsValue s = make_set(vm, klass);
    MsObjMap* dst = MS_AS_MAP(inst_get_field(MS_AS_INSTANCE(s), "_data"));
    MsValue t = MS_BOOL_VAL(true);
    for (int i = 0; i < src_table->capacity; i++) {
        MsVEntry* e = &src_table->entries[i];
        if (e->used && !e->tombstone)
            ms_vtable_set(&dst->table, e->key, t);
    }
    return s;
}

static MsValue set_union(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* a = set_data(argv[0]);
    MsObjMap* b = set_data(argv[1]);
    if (!a || !b) {
        ms_vm_runtime_error(vm, "set.union: both arguments must be Set instances");
        return MS_NIL_VAL();
    }
    /* Copy a, then add all elements of b */
    MsValue result = set_from_map(vm, &a->table);
    if (MS_IS_NIL(result)) return result;
    MsObjMap* dst = MS_AS_MAP(inst_get_field(MS_AS_INSTANCE(result), "_data"));
    MsValue t = MS_BOOL_VAL(true);
    for (int i = 0; i < b->table.capacity; i++) {
        MsVEntry* e = &b->table.entries[i];
        if (e->used && !e->tombstone)
            ms_vtable_set(&dst->table, e->key, t);
    }
    return result;
}

static MsValue set_intersection(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* a = set_data(argv[0]);
    MsObjMap* b = set_data(argv[1]);
    if (!a || !b) {
        ms_vm_runtime_error(vm, "set.intersection: both arguments must be Set instances");
        return MS_NIL_VAL();
    }
    MsObjClass* klass = get_set_class(vm);
    if (!klass) return MS_NIL_VAL();
    MsValue result = make_set(vm, klass);
    MsObjMap* dst = MS_AS_MAP(inst_get_field(MS_AS_INSTANCE(result), "_data"));
    MsValue dummy;
    MsValue t = MS_BOOL_VAL(true);
    for (int i = 0; i < a->table.capacity; i++) {
        MsVEntry* e = &a->table.entries[i];
        if (!e->used || e->tombstone) continue;
        if (ms_vtable_get(&b->table, e->key, &dummy))
            ms_vtable_set(&dst->table, e->key, t);
    }
    return result;
}

static MsValue set_difference(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* a = set_data(argv[0]);
    MsObjMap* b = set_data(argv[1]);
    if (!a || !b) {
        ms_vm_runtime_error(vm, "set.difference: both arguments must be Set instances");
        return MS_NIL_VAL();
    }
    MsObjClass* klass = get_set_class(vm);
    if (!klass) return MS_NIL_VAL();
    MsValue result = make_set(vm, klass);
    MsObjMap* dst = MS_AS_MAP(inst_get_field(MS_AS_INSTANCE(result), "_data"));
    MsValue dummy;
    MsValue t = MS_BOOL_VAL(true);
    for (int i = 0; i < a->table.capacity; i++) {
        MsVEntry* e = &a->table.entries[i];
        if (!e->used || e->tombstone) continue;
        if (!ms_vtable_get(&b->table, e->key, &dummy))
            ms_vtable_set(&dst->table, e->key, t);
    }
    return result;
}

static MsValue set_symmetric_difference(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* a = set_data(argv[0]);
    MsObjMap* b = set_data(argv[1]);
    if (!a || !b) {
        ms_vm_runtime_error(vm, "set.symmetric_difference: both arguments must be Set instances");
        return MS_NIL_VAL();
    }
    MsObjClass* klass = get_set_class(vm);
    if (!klass) return MS_NIL_VAL();
    MsValue result = make_set(vm, klass);
    MsObjMap* dst = MS_AS_MAP(inst_get_field(MS_AS_INSTANCE(result), "_data"));
    MsValue dummy;
    MsValue t = MS_BOOL_VAL(true);
    /* elements in a but not b */
    for (int i = 0; i < a->table.capacity; i++) {
        MsVEntry* e = &a->table.entries[i];
        if (!e->used || e->tombstone) continue;
        if (!ms_vtable_get(&b->table, e->key, &dummy))
            ms_vtable_set(&dst->table, e->key, t);
    }
    /* elements in b but not a */
    for (int i = 0; i < b->table.capacity; i++) {
        MsVEntry* e = &b->table.entries[i];
        if (!e->used || e->tombstone) continue;
        if (!ms_vtable_get(&a->table, e->key, &dummy))
            ms_vtable_set(&dst->table, e->key, t);
    }
    return result;
}

static MsValue set_copy(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* m = set_data(argv[0]);
    if (!m) {
        ms_vm_runtime_error(vm, "set.copy: receiver is not a Set");
        return MS_NIL_VAL();
    }
    return set_from_map(vm, &m->table);
}

/* ------------------------------------------------------------------ */
/* Relational predicates                                               */
/* ------------------------------------------------------------------ */

static MsValue set_is_subset(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* a = set_data(argv[0]);
    MsObjMap* b = set_data(argv[1]);
    if (!a || !b) {
        ms_vm_runtime_error(vm, "set.is_subset: both arguments must be Set instances");
        return MS_NIL_VAL();
    }
    /* a ⊆ b: every element of a is in b */
    MsValue dummy;
    for (int i = 0; i < a->table.capacity; i++) {
        MsVEntry* e = &a->table.entries[i];
        if (!e->used || e->tombstone) continue;
        if (!ms_vtable_get(&b->table, e->key, &dummy))
            return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

static MsValue set_is_superset(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* a = set_data(argv[0]);
    MsObjMap* b = set_data(argv[1]);
    if (!a || !b) {
        ms_vm_runtime_error(vm, "set.is_superset: both arguments must be Set instances");
        return MS_NIL_VAL();
    }
    /* a ⊇ b: every element of b is in a */
    MsValue dummy;
    for (int i = 0; i < b->table.capacity; i++) {
        MsVEntry* e = &b->table.entries[i];
        if (!e->used || e->tombstone) continue;
        if (!ms_vtable_get(&a->table, e->key, &dummy))
            return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

static MsValue set_is_disjoint(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* a = set_data(argv[0]);
    MsObjMap* b = set_data(argv[1]);
    if (!a || !b) {
        ms_vm_runtime_error(vm, "set.is_disjoint: both arguments must be Set instances");
        return MS_NIL_VAL();
    }
    MsValue dummy;
    for (int i = 0; i < a->table.capacity; i++) {
        MsVEntry* e = &a->table.entries[i];
        if (!e->used || e->tombstone) continue;
        if (ms_vtable_get(&b->table, e->key, &dummy))
            return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

static MsValue set_equals(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjMap* a = set_data(argv[0]);
    MsObjMap* b = set_data(argv[1]);
    if (!a || !b) {
        ms_vm_runtime_error(vm, "set.equals: both arguments must be Set instances");
        return MS_NIL_VAL();
    }
    /* Same size and a ⊆ b */
    int na = 0, nb = 0;
    for (int i = 0; i < a->table.capacity; i++) {
        MsVEntry* e = &a->table.entries[i];
        if (e->used && !e->tombstone) na++;
    }
    for (int i = 0; i < b->table.capacity; i++) {
        MsVEntry* e = &b->table.entries[i];
        if (e->used && !e->tombstone) nb++;
    }
    if (na != nb) return MS_BOOL_VAL(false);
    MsValue dummy;
    for (int i = 0; i < a->table.capacity; i++) {
        MsVEntry* e = &a->table.entries[i];
        if (!e->used || e->tombstone) continue;
        if (!ms_vtable_get(&b->table, e->key, &dummy))
            return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

void ms_module_set_init(MsVM* vm, MsObjModule* mod) {
    /* 1. Create the Set class and export it */
    MsObjClass* klass = ms_obj_class_new(vm, intern(vm, "Set"));
    ms_module_export_value(vm, mod, "Set", MS_OBJ_VAL(klass));

    /* 2. Register instance methods on the class */
    static const struct { const char* name; MsNativeFn fn; int arity; } methods[] = {
        /* arity includes self */
        {"add",                  set_add,                  2},
        {"remove",               set_remove,               2},
        {"discard",              set_discard,              2},
        {"contains",             set_contains,             2},
        {"size",                 set_size,                 1},
        {"is_empty",             set_is_empty,             1},
        {"to_list",              set_to_list,              1},
        {"clear",                set_clear,                1},
        {"union",                set_union,                2},
        {"intersection",         set_intersection,         2},
        {"difference",           set_difference,           2},
        {"symmetric_difference", set_symmetric_difference, 2},
        {"copy",                 set_copy,                 1},
        {"is_subset",            set_is_subset,            2},
        {"is_superset",          set_is_superset,          2},
        {"is_disjoint",          set_is_disjoint,          2},
        {"equals",               set_equals,               2},
        {NULL, NULL, 0}
    };

    for (int i = 0; methods[i].name; i++) {
        MsObjString* mname = intern(vm, methods[i].name);
        MsObjNative* nat   = ms_obj_native_new(vm, methods[i].fn,
                                                methods[i].name,
                                                methods[i].arity);
        ms_table_set(&klass->methods, mname, MS_OBJ_VAL(nat));
    }

    /* 3. Register module-level constructors */
    static const MsNativeDef defs[] = {
        {"new",       set_new,       -1},
        {"from_list", set_from_list,  1},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);
}
