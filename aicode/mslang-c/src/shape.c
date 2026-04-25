#include "ms/shape.h"
#include "ms/vm.h"
#include "ms/table.h"
#include "ms/object.h"
#include "ms/memory.h"
#include <stdlib.h>
#include <string.h>

/* Global shape ID counter (not thread-safe; single VM assumption). */
static uint32_t s_shape_id_counter = 0;

/* ---- SmallMap helpers ---- */

/* Lookup key in SmallMap; returns slot index on hit, -1 on miss. */
static int small_map_get(const MsSmallMap* m, MsObjString* key) {
    for (int i = 0; i < m->count; i++) {
        if (m->data[i].key == key)
            return (int)m->data[i].value;
    }
    if (m->overflow) {
        MsValue v;
        if (ms_table_get(m->overflow, key, &v))
            return (int)MS_AS_INT(v);
    }
    return -1;
}

/* Insert or update key->value in SmallMap. */
static void small_map_set(struct MsVM* vm, MsSmallMap* m,
                           MsObjString* key, uint32_t value) {
    /* Update existing entry in inline array */
    for (int i = 0; i < m->count; i++) {
        if (m->data[i].key == key) {
            m->data[i].value = value;
            return;
        }
    }
    /* Check overflow */
    if (m->overflow) {
        MsValue v;
        if (ms_table_get(m->overflow, key, &v)) {
            ms_table_set(m->overflow, key, MS_INT_VAL((ms_i64)value));
            return;
        }
    }
    /* Insert new entry in inline array */
    if (m->count < MS_SMALL_MAP_INLINE) {
        m->data[m->count].key   = key;
        m->data[m->count].value = value;
        m->count++;
        return;
    }
    /* Spill to overflow table */
    if (!m->overflow) {
        m->overflow = (MsTable*)MS_ALLOC(vm, MsTable, 1);
        ms_table_init(m->overflow);
    }
    ms_table_set(m->overflow, key, MS_INT_VAL((ms_i64)value));
}

/* Free SmallMap overflow table if present. */
static void small_map_free(struct MsVM* vm, MsSmallMap* m) {
    if (m->overflow) {
        ms_table_free(m->overflow);
        MS_FREE(vm, MsTable, m->overflow);
        m->overflow = NULL;
    }
}

/* Deep-copy src SmallMap into dst (dst must be zero-initialised). */
static void small_map_copy(struct MsVM* vm, MsSmallMap* dst,
                            const MsSmallMap* src) {
    *dst = *src;              /* copy inline array + count */
    dst->overflow = NULL;     /* don't share the overflow table */
    if (src->overflow) {
        dst->overflow = (MsTable*)MS_ALLOC(vm, MsTable, 1);
        ms_table_init(dst->overflow);
        ms_table_add_all(dst->overflow, src->overflow);
    }
}

/* ---- Shape allocation ---- */

MsShape* ms_shape_new(struct MsVM* vm) {
    MsShape* s = (MsShape*)MS_ALLOC(vm, MsShape, 1);
    s->id          = s_shape_id_counter++;
    memset(&s->slots, 0, sizeof(s->slots));
    s->slot_count  = 0;
    s->trans       = NULL;
    s->trans_count = 0;
    s->trans_cap   = 0;
    return s;
}

/* Free a shape (called only from vm_gc when cleaning up all shapes). */
void ms_shape_free(struct MsVM* vm, MsShape* s) {
    small_map_free(vm, &s->slots);
    if (s->trans) {
        MS_FREE_ARRAY(vm, MsTransEntry, s->trans, s->trans_cap);
        s->trans = NULL;
    }
    MS_FREE(vm, MsShape, s);
}

/* ---- Transition lookup/create ---- */

static MsShape* trans_get(const MsShape* s, MsObjString* key) {
    for (int i = 0; i < s->trans_count; i++) {
        if (s->trans[i].key == key)
            return s->trans[i].child;
    }
    return NULL;
}

static void trans_set(struct MsVM* vm, MsShape* s,
                      MsObjString* key, MsShape* child) {
    if (s->trans_count >= s->trans_cap) {
        int new_cap = s->trans_cap < 4 ? 4 : s->trans_cap * 2;
        s->trans = (MsTransEntry*)ms_reallocate(
            vm, s->trans,
            sizeof(MsTransEntry) * (size_t)s->trans_cap,
            sizeof(MsTransEntry) * (size_t)new_cap);
        s->trans_cap = new_cap;
    }
    s->trans[s->trans_count].key   = key;
    s->trans[s->trans_count].child = child;
    s->trans_count++;
}

/* ---- Public API ---- */

MsShape* ms_shape_transition(struct MsVM* vm, MsShape* shape,
                              MsObjString* name) {
    /* Check existing transition */
    MsShape* child = trans_get(shape, name);
    if (child) return child;

    /* Create new child shape */
    child = ms_shape_new(vm);
    /* Copy parent's slot map */
    small_map_copy(vm, &child->slots, &shape->slots);
    child->slot_count = shape->slot_count;

    /* Add new property -> next slot index */
    small_map_set(vm, &child->slots, name, child->slot_count);
    child->slot_count++;

    /* Record transition in parent */
    trans_set(vm, shape, name, child);

    return child;
}

int ms_shape_find_slot(MsShape* shape, MsObjString* name) {
    return small_map_get(&shape->slots, name);
}
