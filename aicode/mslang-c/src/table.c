#include "ms/table.h"
#include <stdlib.h>
#include <string.h>

// count * 4 > capacity * 3  equivalent to  count/capacity > 0.75
#define TABLE_LOAD_EXCEEDED(count, cap) ((count) * 4 > (cap) * 3)

static bool entry_is_live(const MsEntry* e) {
    return e->key != NULL && e->key != MS_TABLE_TOMBSTONE;
}

void ms_table_init(MsTable* t) {
    t->entries = NULL;
    t->count = 0;
    t->live_count = 0;
    t->capacity = 0;
}

void ms_table_free(MsTable* t) {
    free(t->entries);
    ms_table_init(t);
}

static MsEntry* find_entry(MsEntry* entries, int capacity, MsObjString* key) {
    uint32_t index = key->hash & (uint32_t)(capacity - 1);
    MsEntry* tombstone = NULL;
    for (;;) {
        MsEntry* entry = &entries[index];
        if (entry->key == NULL)              return tombstone != NULL ? tombstone : entry;
        if (entry->key == MS_TABLE_TOMBSTONE) { if (!tombstone) tombstone = entry; }
        else if (entry->key == key)          return entry;
        index = (index + 1) & (uint32_t)(capacity - 1);
    }
}

static void adjust_capacity(MsTable* t, int new_cap) {
    MsEntry* entries = (MsEntry*)calloc((size_t)new_cap, sizeof(MsEntry));
    if (!entries) abort();

    // Rehash live entries; tombstones drop out, so count resets to live_count.
    t->count = 0;
    t->live_count = 0;
    for (int i = 0; i < t->capacity; i++) {
        MsEntry* src = &t->entries[i];
        if (!entry_is_live(src)) continue;
        MsEntry* dst = find_entry(entries, new_cap, src->key);
        dst->key = src->key;
        dst->value = src->value;
        t->count++;
        t->live_count++;
    }

    free(t->entries);
    t->entries = entries;
    t->capacity = new_cap;
}

bool ms_table_set(MsTable* t, MsObjString* key, MsValue val) {
    if (TABLE_LOAD_EXCEEDED(t->count + 1, t->capacity)) {
        int new_cap = t->capacity < 8 ? 8 : t->capacity * 2;
        adjust_capacity(t, new_cap);
    }
    MsEntry* entry = find_entry(t->entries, t->capacity, key);
    bool was_tombstone = (entry->key == MS_TABLE_TOMBSTONE);
    bool is_new = !entry_is_live(entry);
    entry->key = key;
    entry->value = val;
    if (is_new) {
        t->live_count++;
        if (!was_tombstone) t->count++;
    }
    return is_new;
}

bool ms_table_get(MsTable* t, MsObjString* key, MsValue* out) {
    if (t->capacity == 0) return false;
    MsEntry* entry = find_entry(t->entries, t->capacity, key);
    if (!entry_is_live(entry)) return false;
    *out = entry->value;
    return true;
}

bool ms_table_delete(MsTable* t, MsObjString* key) {
    if (t->capacity == 0) return false;
    MsEntry* entry = find_entry(t->entries, t->capacity, key);
    if (!entry_is_live(entry)) return false;
    entry->key = MS_TABLE_TOMBSTONE;
    entry->value = MS_BOOL_VAL(true);
    t->live_count--;
    return true;
}

void ms_table_add_all(MsTable* dst, MsTable* src) {
    for (int i = 0; i < src->capacity; i++) {
        MsEntry* e = &src->entries[i];
        if (entry_is_live(e)) ms_table_set(dst, e->key, e->value);
    }
}

MsObjString* ms_table_find_string(MsTable* t, const char* chars, int length,
                                   uint32_t hash) {
    if (t->capacity == 0) return NULL;
    uint32_t index = hash & (uint32_t)(t->capacity - 1);
    for (;;) {
        MsEntry* entry = &t->entries[index];
        if (entry->key == NULL) return NULL;
        if (entry->key != MS_TABLE_TOMBSTONE &&
            entry->key->hash == hash &&
            entry->key->length == length &&
            memcmp(entry->key->chars, chars, (size_t)length) == 0) {
            return entry->key;
        }
        index = (index + 1) & (uint32_t)(t->capacity - 1);
    }
}

void ms_table_remove_white(MsTable* t) {
    for (int i = 0; i < t->capacity; i++) {
        MsEntry* e = &t->entries[i];
        if (entry_is_live(e) && !e->key->obj.is_marked) {
            e->key = MS_TABLE_TOMBSTONE;
            e->value = MS_BOOL_VAL(true);
            t->live_count--;
        }
    }
}
