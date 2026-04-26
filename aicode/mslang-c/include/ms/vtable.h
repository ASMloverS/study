#pragma once
#include "ms/value.h"
#include <stdbool.h>

typedef struct {
    MsValue key;
    MsValue value;
    bool used;
    bool tombstone;
} MsVEntry;

typedef struct {
    MsVEntry* entries;
    int count;
    int capacity;
} MsValueTable;

void ms_vtable_init(MsValueTable* t);
void ms_vtable_free(MsValueTable* t);
bool ms_vtable_set(MsValueTable* t, MsValue key, MsValue val);
bool ms_vtable_get(MsValueTable* t, MsValue key, MsValue* out);
bool ms_vtable_delete(MsValueTable* t, MsValue key);
