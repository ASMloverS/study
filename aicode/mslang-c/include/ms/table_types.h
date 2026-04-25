#pragma once
/* table_types.h - MsTable struct without depending on object.h.
   Forward-declares MsObjString so object.h can include this header
   before defining MsObjString itself. */
#include "ms/value.h"

/* Forward declaration - full definition is in object.h */
typedef struct MsObjString MsObjString;

typedef struct {
    MsObjString* key;
    MsValue value;
} MsEntry;

typedef struct MsTable {
    MsEntry* entries;
    int count;
    int live_count;
    int capacity;
} MsTable;
