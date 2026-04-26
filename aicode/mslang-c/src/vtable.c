#include "ms/vtable.h"
#include "ms/object.h"
#include <stdlib.h>
#include <string.h>

static uint32_t hash_value(MsValue v) {
    switch (v.type) {
    case MS_VAL_NIL:    return 2u;  /* distinct from bool 0/1 */
    case MS_VAL_BOOL:   return MS_AS_BOOL(v) ? 1u : 0u;
    case MS_VAL_INT:
    case MS_VAL_NUMBER: {
        /* Normalize so int(n) == float(n) hash identically.
           Also maps -0.0 to 0u (since -0.0 == 0.0 in IEEE 754). */
        double d = MS_IS_INT(v) ? (double)MS_AS_INT(v) : MS_AS_NUMBER(v);
        if (d == 0.0) return 0u;
        union { double d; uint64_t u; } cv;
        cv.d = d;
        return (uint32_t)(cv.u ^ (cv.u >> 32));
    }
    case MS_VAL_OBJECT: {
        MsObject* obj = MS_AS_OBJECT(v);
        if (obj->type == MS_OBJ_STRING) return ((MsObjString*)obj)->hash;
        if (obj->type == MS_OBJ_TUPLE)  return ((MsObjTuple*)obj)->hash;
        return (uint32_t)(uintptr_t)obj;
    }
    }
    return 0u;
}

static bool values_equal_raw(MsValue a, MsValue b) {
    return ms_value_equals(a, b);
}

void ms_vtable_init(MsValueTable* t) {
    t->entries  = NULL;
    t->count    = 0;
    t->capacity = 0;
}

void ms_vtable_free(MsValueTable* t) {
    free(t->entries);
    ms_vtable_init(t);
}

static MsVEntry* find_entry(MsVEntry* entries, int cap, MsValue key) {
    uint32_t idx = hash_value(key) & (uint32_t)(cap - 1);
    MsVEntry* tombstone = NULL;
    for (;;) {
        MsVEntry* e = &entries[idx];
        if (!e->used) {
            if (!e->tombstone) return tombstone ? tombstone : e;
            if (!tombstone) tombstone = e;
        } else if (values_equal_raw(e->key, key)) {
            return e;
        }
        idx = (idx + 1) & (uint32_t)(cap - 1);
    }
}

static void grow(MsValueTable* t) {
    int new_cap = t->capacity < 8 ? 8 : t->capacity * 2;
    MsVEntry* new_entries = (MsVEntry*)calloc((size_t)new_cap, sizeof(MsVEntry));
    if (!new_entries) abort();
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* src = &t->entries[i];
        if (!src->used) continue;
        MsVEntry* dst = find_entry(new_entries, new_cap, src->key);
        *dst = *src;
    }
    free(t->entries);
    t->entries  = new_entries;
    t->capacity = new_cap;
}

bool ms_vtable_set(MsValueTable* t, MsValue key, MsValue val) {
    if (t->count + 1 > t->capacity * 3 / 4) grow(t);
    MsVEntry* e = find_entry(t->entries, t->capacity, key);
    bool is_new = !e->used;
    if (is_new) t->count++;
    e->key       = key;
    e->value     = val;
    e->used      = true;
    e->tombstone = false;
    return is_new;
}

bool ms_vtable_get(MsValueTable* t, MsValue key, MsValue* out) {
    if (!t->entries) return false;
    MsVEntry* e = find_entry(t->entries, t->capacity, key);
    if (!e->used) return false;
    *out = e->value;
    return true;
}

bool ms_vtable_delete(MsValueTable* t, MsValue key) {
    if (!t->entries) return false;
    MsVEntry* e = find_entry(t->entries, t->capacity, key);
    if (!e->used) return false;
    e->used      = false;
    e->tombstone = true;
    t->count--;
    return true;
}
