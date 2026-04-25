#pragma once
#include "ms/common.h"
#include "ms/consts.h"
#include "ms/table_types.h"

/* Forward declarations */
struct MsVM;
typedef struct MsObjString MsObjString;

/* ---- SmallMap ---- */

#define MS_SMALL_MAP_INLINE 8

typedef struct {
    MsObjString* key;
    uint32_t     value;
} MsSmallEntry;

/* Small linear map for up to MS_SMALL_MAP_INLINE entries.
   Spills to MsTable* overflow when full. */
typedef struct {
    MsSmallEntry data[MS_SMALL_MAP_INLINE];
    int          count;
    MsTable*     overflow;  /* NULL until spill */
} MsSmallMap;

/* ---- MsShape (hidden class) ---- */

/* Transition entry maps a property name to a child shape pointer */
typedef struct {
    MsObjString*    key;
    struct MsShape* child;
} MsTransEntry;

typedef struct MsShape {
    uint32_t      id;
    MsSmallMap    slots;        /* property name -> slot index */
    uint32_t      slot_count;
    /* Shape transitions: dynamically grown array of (key -> child shape) */
    MsTransEntry* trans;        /* heap-allocated; NULL if no transitions yet */
    int           trans_count;
    int           trans_cap;
} MsShape;

/* ---- Inline Cache ---- */

typedef enum {
    MS_IC_NONE,
    MS_IC_FIELD,
    MS_IC_METHOD,
    MS_IC_GETTER,
    MS_IC_SETTER,
} MsICKind;

typedef struct {
    uint32_t  shape_id;
    uint32_t  slot_index;
    MsICKind  kind;
    MsValue   cached;   /* cached method value (for MS_IC_METHOD) */
} MsICEntry;

typedef struct MsInlineCache {
    MsICEntry entries[MS_IC_PIC_SIZE];
    uint8_t   count;
    bool      megamorphic;
} MsInlineCache;

/* ---- API ---- */

/* Create a new empty root shape (id=0 means "root generated from counter"). */
MsShape* ms_shape_new(struct MsVM* vm);

/* Find or create the child shape reached by adding property 'name' to 'shape'.
   The new shape inherits all existing slots and appends one more. */
MsShape* ms_shape_transition(struct MsVM* vm, MsShape* shape, MsObjString* name);

/* Return the slot index for 'name', or -1 if not found. */
int ms_shape_find_slot(MsShape* shape, MsObjString* name);

/* Free all memory owned by a shape (called from GC). */
void ms_shape_free(struct MsVM* vm, MsShape* shape);

/* Accessors for instance field arrays (SBO helper) */
MS_INLINE MsValue* ms_shape_field_ptr(MsValue* inline_fields,
                                           MsValue* overflow_fields,
                                           int slot) {
    if (slot < MS_SBO_FIELDS) return &inline_fields[slot];
    return &overflow_fields[slot - MS_SBO_FIELDS];
}
