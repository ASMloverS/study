#pragma once
#include "ms/value.h"
#include "ms/object.h"

// Sentinel for deleted slots: non-NULL, never a valid string ptr.
#define MS_TABLE_TOMBSTONE ((MsObjString*)(uintptr_t)1)

typedef struct {
    MsObjString* key;
    MsValue value;
} MsEntry;

typedef struct {
    MsEntry* entries;
    int count;       // live + tombstones (used for load-factor)
    int live_count;  // live entries only
    int capacity;    // always a power of 2
} MsTable;

void         ms_table_init(MsTable* t);
void         ms_table_free(MsTable* t);
bool         ms_table_set(MsTable* t, MsObjString* key, MsValue val);
bool         ms_table_get(MsTable* t, MsObjString* key, MsValue* out);
bool         ms_table_delete(MsTable* t, MsObjString* key);
void         ms_table_add_all(MsTable* dst, MsTable* src);
MsObjString* ms_table_find_string(MsTable* t, const char* chars, int length, uint32_t hash);
void         ms_table_remove_white(MsTable* t);
