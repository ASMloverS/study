# Task 03: Hash Table

> **Agentic workers:** Use superpowers:executing-plans.

**Goal:** Open-addressing hash table w/ `MsObjString*` keys — globals, string interning, method tables.
**Deps:** T02
**Produces:** `MsTable` insert/get/delete/find_string; unit tests passing.

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/table.h` | `MsTable` def + API |
| Create | `src/table.c` | Hash table impl |
| Create | `tests/unit/test_table.c` | Unit tests |

## API

```c
// include/ms/table.h
#pragma once
#include "ms/value.h"

typedef struct MsObjString MsObjString;

// Tombstone: non-NULL invalid ptr for deleted slots
#define MS_TABLE_TOMBSTONE ((MsObjString*)(uintptr_t)1)

typedef struct {
    MsObjString* key;  // NULL=empty, TOMBSTONE=deleted
    MsValue value;
} MsEntry;

typedef struct {
    MsEntry* entries;
    int count;       // live + tombstones (load factor)
    int live_count;  // live only
    int capacity;    // always pow2
} MsTable;

void         ms_table_init(MsTable* t);
void         ms_table_free(MsTable* t);
bool         ms_table_set(MsTable* t, MsObjString* key, MsValue val);  // true if new key
bool         ms_table_get(MsTable* t, MsObjString* key, MsValue* out);
bool         ms_table_delete(MsTable* t, MsObjString* key);
void         ms_table_add_all(MsTable* dst, MsTable* src);
MsObjString* ms_table_find_string(MsTable* t, const char* chars, int length, uint32_t hash);
void         ms_table_remove_white(MsTable* t);  // GC: remove unmarked string entries
```

## Impl Notes

- **Hashing:** uses `ObjString->hash` (FNV-1a, impl in T04)
- **Probing:** linear; `index = hash & (capacity - 1)`
- **Load factor:** resize when count (incl. tombstones) > capacity × 3/4
- **Capacity:** pow2; init 0, alloc 8 on first insert
- **Tombstone:** on delete, set key=`MS_TABLE_TOMBSTONE`, val=`MS_BOOL_VAL(true)`; reusable on insert
- **`find_string`:** search by char content + length (not ptr) for interning; hash check before content compare
- **`remove_white`:** called during GC sweep; removes unmarked string entries from intern table

```c
// Core lookup
static MsEntry* find_entry(MsEntry* entries, int capacity, MsObjString* key) {
    uint32_t index = key->hash & (uint32_t)(capacity - 1);
    MsEntry* tombstone = NULL;
    for (;;) {
        MsEntry* entry = &entries[index];
        if (entry->key == NULL) {
            return tombstone != NULL ? tombstone : entry;  // empty slot
        } else if (entry->key == MS_TABLE_TOMBSTONE) {
            if (tombstone == NULL) tombstone = entry;
        } else if (entry->key == key) {
            return entry;  // interned string → ptr compare
        }
        index = (index + 1) & (uint32_t)(capacity - 1);
    }
}
```

## Unit Tests

```c
// tests/unit/test_table.c
// Full test requires MsObjString from T04. Use mock or defer until T04.

#include "test_assert.h"
#include "ms/table.h"

// Minimal mock — replace with real MsObjString after T04
typedef struct {
    MsObject obj;
    uint32_t hash;
    int length;
    char data[32];
} MockString;

static MockString make_mock(const char* s, uint32_t h) {
    MockString m = {0};
    m.obj.type = MS_OBJ_STRING;
    m.hash = h;
    m.length = (int)strlen(s);
    strncpy(m.data, s, 31);
    return m;
}

static void test_set_get(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k1 = make_mock("x", 100);
    ms_table_set(&t, (MsObjString*)&k1, MS_INT_VAL(42));
    MsValue out;
    TEST_ASSERT(ms_table_get(&t, (MsObjString*)&k1, &out));
    TEST_ASSERT(MS_AS_INT(out) == 42);
    ms_table_free(&t);
}

static void test_delete(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k1 = make_mock("a", 200);
    ms_table_set(&t, (MsObjString*)&k1, MS_INT_VAL(1));
    TEST_ASSERT(ms_table_delete(&t, (MsObjString*)&k1));
    MsValue out;
    TEST_ASSERT(!ms_table_get(&t, (MsObjString*)&k1, &out));
    ms_table_free(&t);
}

static void test_grow(void) {
    MsTable t;
    ms_table_init(&t);
    MockString keys[20];
    for (int i = 0; i < 20; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "k%d", i);
        keys[i] = make_mock(buf, (uint32_t)(i * 7 + 1));
        ms_table_set(&t, (MsObjString*)&keys[i], MS_INT_VAL(i));
    }
    TEST_ASSERT(t.live_count == 20);
    TEST_ASSERT(t.capacity >= 20);
    MsValue out;
    TEST_ASSERT(ms_table_get(&t, (MsObjString*)&keys[15], &out));
    TEST_ASSERT(MS_AS_INT(out) == 15);
    ms_table_free(&t);
}

int main(void) {
    test_set_get();
    test_delete();
    test_grow();
    printf("test_table: all passed\n");
    return 0;
}
```

## .ms Integration Tests

None — internal structure; tested indirectly via variables, globals, method tables.
