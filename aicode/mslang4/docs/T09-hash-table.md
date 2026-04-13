# T09: Hash Table

**Phase**: 2 — Core Data Types
**Deps**: T07 (Value System), T08 (Object System — Strings)
**Complexity**: High

## Goal

Hash table for globals, string interning, class methods, instance fields, module exports. Open addressing, linear probing, tombstones for deletion.

## Files

| File | Purpose |
|------|---------|
| `src/table.h` | Table types + API |
| `src/table.c` | Table impl |

## TDD Cycles

### Cycle 1: Table Init + Free

**RED** — Create `tests/unit/test_table.c`:

```c
#include "table.h"
#include "object.h"
#include "value.h"
#include <stdio.h>
#include <assert.h>

static void test_table_init_free(void) {
    MsTable table;
    ms_table_init(&table);
    assert(table.entries == NULL);
    assert(table.count == 0);
    assert(table.capacity == 0);

    ms_table_free(&table);
    assert(table.entries == NULL);
    assert(table.count == 0);
    assert(table.capacity == 0);
    printf("  test_table_init_free PASSED\n");
}

int main(void) {
    printf("Running table tests...\n");
    test_table_init_free();
    printf("All table tests passed.\n");
    return 0;
}
```

`table.h` doesn't exist → compile error.

**Verify RED**: `gcc -I src -o build/test_table tests/unit/test_table.c src/table.c src/object.c src/value.c` → `table.h` not found

**GREEN** — Create `src/table.h`:

```c
#ifndef MS_TABLE_H
#define MS_TABLE_H

#include "value.h"
#include "object.h"

#define MS_TABLE_MAX_LOAD 0.75

typedef struct { MsString* key; MsValue value; } MsTableEntry;
typedef struct { MsTableEntry* entries; int count; int capacity; } MsTable;

void ms_table_init(MsTable* table);
void ms_table_free(MsTable* table);
bool ms_table_set(MsTable* table, MsString* key, MsValue value);
bool ms_table_get(MsTable* table, MsString* key, MsValue* outValue);
bool ms_table_remove(MsTable* table, MsString* key);
void ms_table_add_all(MsTable* from, MsTable* to);
MsString* ms_table_find_string(MsTable* table, const char* chars, int length, uint32_t hash);
void ms_table_remove_white(MsTable* table);
void ms_table_mark(MsTable* table);

#endif
```

Create `src/table.c`:

```c
#include "table.h"
#include "common.h"
#include <stdlib.h>

void ms_table_init(MsTable* table) {
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}

void ms_table_free(MsTable* table) {
    MS_FREE_ARRAY(MsTableEntry, table->entries, table->capacity);
    ms_table_init(table);
}
```

**Verify GREEN**: build + run → test passes.

**REFACTOR**: None needed.

---

### Cycle 2: Table Set + Get (Single Entry)

**RED** — Add to `test_table.c`:

```c
static void test_table_set_get(void) {
    MsTable table;
    ms_table_init(&table);

    MsString* key = ms_string_copy("x", 1);
    MsValue val = ms_number_val(42.0);

    bool is_new = ms_table_set(&table, key, val);
    assert(is_new);
    assert(table.count == 1);

    MsValue out;
    bool found = ms_table_get(&table, key, &out);
    assert(found);
    assert(ms_as_number(out) == 42.0);

    ms_table_free(&table);
    ms_object_free((MsObject*)key);
    printf("  test_table_set_get PASSED\n");
}
```

`ms_table_set`/`ms_table_get` undefined → linker error.

**GREEN** — Add to `src/table.c`:

```c
static MsTableEntry* ms_table_find_entry(MsTableEntry* entries, int capacity, MsString* key) {
    uint32_t index = key->hash % capacity;
    MsTableEntry* tombstone = NULL;
    for (;;) {
        MsTableEntry* entry = &entries[index];
        if (entry->key == NULL) {
            if (MS_IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            return entry;
        }
        index = (index + 1) % capacity;
    }
}
```

Tombstone sentinel: `key == NULL && value` non-nil. `MS_IS_NIL` distinguishes empty from tombstone.

```c
static void ms_table_adjust_capacity(MsTable* table, int new_capacity) {
    MsTableEntry* new_entries = MS_ALLOCATE(MsTableEntry, new_capacity);
    for (int i = 0; i < new_capacity; i++) {
        new_entries[i].key = NULL;
        new_entries[i].value = ms_nil_val();
    }
    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        MsTableEntry* entry = &table->entries[i];
        if (entry->key == NULL) continue;
        MsTableEntry* dest = ms_table_find_entry(new_entries, new_capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
    MS_FREE_ARRAY(MsTableEntry, table->entries, table->capacity);
    table->entries = new_entries;
    table->capacity = new_capacity;
}

bool ms_table_set(MsTable* table, MsString* key, MsValue value) {
    if (table->count + 1 > table->capacity * MS_TABLE_MAX_LOAD) {
        int capacity = MS_GROW_CAPACITY(table->capacity);
        ms_table_adjust_capacity(table, capacity);
    }
    MsTableEntry* entry = ms_table_find_entry(table->entries, table->capacity, key);
    bool is_new_key = entry->key == NULL;
    if (is_new_key && ms_is_nil(entry->value)) table->count++;
    entry->key = key;
    entry->value = value;
    return is_new_key;
}

bool ms_table_get(MsTable* table, MsString* key, MsValue* outValue) {
    if (table->count == 0) return false;
    MsTableEntry* entry = ms_table_find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;
    *outValue = entry->value;
    return true;
}
```

Logic:
1. Load factor > 75% → grow via `MS_GROW_CAPACITY`, rehash all entries (skip NULL keys + tombstones)
2. Probe: `index = key->hash % capacity`, linear probe
3. Existing key → update value, return false. Empty/tombstone → insert, count++, return true
4. Get: probe by hash. Key match → set outValue, return true. Empty → return false

**Verify GREEN**: build + run → both tests pass.

**REFACTOR**: None needed.

---

### Cycle 3: Table Set Overwrite

**RED** — Add to `test_table.c`:

```c
static void test_table_overwrite(void) {
    MsTable table;
    ms_table_init(&table);

    MsString* key = ms_string_copy("x", 1);

    bool is_new1 = ms_table_set(&table, key, ms_number_val(1.0));
    assert(is_new1 == true);

    bool is_new2 = ms_table_set(&table, key, ms_number_val(2.0));
    assert(is_new2 == false);
    assert(table.count == 1);

    MsValue out;
    bool found = ms_table_get(&table, key, &out);
    assert(found);
    assert(ms_as_number(out) == 2.0);

    ms_table_free(&table);
    ms_object_free((MsObject*)key);
    printf("  test_table_overwrite PASSED\n");
}
```

**Verify RED**: Should compile — Cycle 2 already returns false for existing keys. Passes immediately.

**Verify GREEN**: build + run → all three tests pass.

**REFACTOR**: None needed.

---

### Cycle 4: Table Remove + Tombstones

**RED** — Add to `test_table.c`:

```c
static void test_table_remove_tombstone(void) {
    MsTable table;
    ms_table_init(&table);

    MsString* keyA = ms_string_copy("a", 1);
    MsString* keyB = ms_string_copy("b", 1);
    MsString* keyC = ms_string_copy("c", 1);

    ms_table_set(&table, keyA, ms_number_val(1.0));
    ms_table_set(&table, keyB, ms_number_val(2.0));
    ms_table_set(&table, keyC, ms_number_val(3.0));

    bool removed = ms_table_remove(&table, keyB);
    assert(removed);

    MsValue out;
    assert(!ms_table_get(&table, keyB, &out));
    assert(ms_table_get(&table, keyA, &out));
    assert(ms_as_number(out) == 1.0);
    assert(ms_table_get(&table, keyC, &out));
    assert(ms_as_number(out) == 3.0);

    MsString* keyD = ms_string_copy("d", 1);
    assert(!ms_table_remove(&table, keyD));

    ms_table_free(&table);
    ms_object_free((MsObject*)keyA);
    ms_object_free((MsObject*)keyB);
    ms_object_free((MsObject*)keyC);
    ms_object_free((MsObject*)keyD);
    printf("  test_table_remove_tombstone PASSED\n");
}
```

`ms_table_remove` undefined → linker error.

**GREEN** — Add to `src/table.c`:

```c
bool ms_table_remove(MsTable* table, MsString* key) {
    if (table->count == 0) return false;
    MsTableEntry* entry = ms_table_find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;
    entry->key = NULL;
    entry->value = ms_bool_val(true);
    return true;
}
```

Tombstone: `key=NULL`, `value=ms_bool_val(true)` (non-nil sentinel).

**Verify GREEN**: build + run → all four tests pass.

**REFACTOR**: None needed.

---

### Cycle 5: Table Growth (Dynamic Resizing)

**RED** — Add to `test_table.c`:

```c
static void test_table_growth(void) {
    MsTable table;
    ms_table_init(&table);

    MsString* keys[200];
    for (int i = 0; i < 200; i++) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "key_%d", i);
        keys[i] = ms_string_copy(buf, len);
        ms_table_set(&table, keys[i], ms_number_val((double)i));
    }

    assert(table.count == 200);

    for (int i = 0; i < 200; i++) {
        MsValue out;
        bool found = ms_table_get(&table, keys[i], &out);
        assert(found);
        assert(ms_as_number(out) == (double)i);
    }

    ms_table_free(&table);
    for (int i = 0; i < 200; i++) {
        ms_object_free((MsObject*)keys[i]);
    }
    printf("  test_table_growth PASSED\n");
}
```

Stress/verification — growth from Cycle 2 `ms_table_adjust_capacity` handles it.

**Verify GREEN**: build + run → all five tests pass. Verify no leaks with ASan.

---

### Cycle 6: `find_string`

**RED** — Add to `test_table.c`:

```c
static void test_find_string(void) {
    MsTable table;
    ms_table_init(&table);

    MsString* s1 = ms_string_copy("hello", 5);
    MsString* s2 = ms_string_copy("world", 5);
    ms_table_set(&table, s1, ms_number_val(1.0));
    ms_table_set(&table, s2, ms_number_val(2.0));

    uint32_t hash = ms_string_hash("hello", 5);
    MsString* found = ms_table_find_string(&table, "hello", 5, hash);
    assert(found == s1);

    MsString* not_found = ms_table_find_string(&table, "nope", 4, ms_string_hash("nope", 4));
    assert(not_found == NULL);

    ms_table_free(&table);
    ms_object_free((MsObject*)s1);
    ms_object_free((MsObject*)s2);
    printf("  test_find_string PASSED\n");
}
```

`ms_table_find_string` undefined → linker error.

**GREEN** — Add to `src/table.c`:

```c
MsString* ms_table_find_string(MsTable* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) return NULL;
    uint32_t index = hash % table->capacity;
    for (;;) {
        MsTableEntry* entry = &table->entries[index];
        if (entry->key == NULL) {
            if (ms_is_nil(entry->value)) return NULL;
        } else if (entry->key->length == length &&
                   entry->key->hash == hash &&
                   memcmp(entry->key->chars, chars, length) == 0) {
            return entry->key;
        }
        index = (index + 1) % table->capacity;
    }
}
```

Probe from `hash % capacity`, compare length → hash → chars. Skip tombstones.

**Verify GREEN**: build + run → all six tests pass.

---

### Cycle 7: `add_all`

**RED** — Add to `test_table.c`:

```c
static void test_add_all(void) {
    MsTable src, dst;
    ms_table_init(&src);
    ms_table_init(&dst);

    MsString* k1 = ms_string_copy("a", 1);
    MsString* k2 = ms_string_copy("b", 1);
    ms_table_set(&src, k1, ms_number_val(10.0));
    ms_table_set(&src, k2, ms_number_val(20.0));

    ms_table_add_all(&src, &dst);
    assert(dst.count == 2);

    MsValue out;
    assert(ms_table_get(&dst, k1, &out));
    assert(ms_as_number(out) == 10.0);
    assert(ms_table_get(&dst, k2, &out));
    assert(ms_as_number(out) == 20.0);

    ms_table_free(&src);
    ms_table_free(&dst);
    ms_object_free((MsObject*)k1);
    ms_object_free((MsObject*)k2);
    printf("  test_add_all PASSED\n");
}
```

`ms_table_add_all` undefined → linker error.

**GREEN** — Add to `src/table.c`:

```c
void ms_table_add_all(MsTable* from, MsTable* to) {
    for (int i = 0; i < from->capacity; i++) {
        MsTableEntry* entry = &from->entries[i];
        if (entry->key != NULL) {
            ms_table_set(to, entry->key, entry->value);
        }
    }
}
```

Iterate src, skip NULL keys + tombstones, `ms_table_set` into dst.

**Verify GREEN**: build + run → all seven tests pass.

---

### Cycle 8: GC Helpers (`remove_white`, `mark`)

**RED** — Add to `test_table.c`:

```c
static void test_gc_helpers(void) {
    MsTable table;
    ms_table_init(&table);

    MsString* k1 = ms_string_copy("keep", 4);
    MsString* k2 = ms_string_copy("remove", 6);
    k1->base.isMarked = true;
    k2->base.isMarked = false;

    ms_table_set(&table, k1, ms_number_val(1.0));
    ms_table_set(&table, k2, ms_number_val(2.0));

    ms_table_remove_white(&table);
    assert(table.count == 1);

    MsValue out;
    assert(ms_table_get(&table, k1, &out));
    assert(!ms_table_get(&table, k2, &out));

    ms_table_mark(&table);

    ms_table_free(&table);
    ms_object_free((MsObject*)k1);
    ms_object_free((MsObject*)k2);
    printf("  test_gc_helpers PASSED\n");
}
```

`ms_table_remove_white`/`ms_table_mark` undefined → linker error.

**GREEN** — Add to `src/table.c`:

```c
void ms_table_remove_white(MsTable* table) {
    for (int i = 0; i < table->capacity; i++) {
        MsTableEntry* entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->base.isMarked) {
            ms_table_remove(table, entry->key);
        }
    }
}

void ms_table_mark(MsTable* table) {
    for (int i = 0; i < table->capacity; i++) {
        MsTableEntry* entry = &table->entries[i];
        if (entry->key != NULL) {
            entry->key->base.isMarked = true;
        }
    }
}
```

`remove_white`: delete entries where `key->isMarked == false`.
`mark`: simplified — sets `isMarked` directly. Real impl in T17 uses GC mark calls.

**Verify GREEN**: build + run → all eight tests pass.

**REFACTOR**: `ms_table_mark` updated in T17 to use actual GC marking.

## Acceptance Criteria

- [x] Init/free cycle, no leak
- [x] Set → get returns correct value
- [x] Set existing key updates value (returns false)
- [x] Remove → get returns false
- [x] Tombstones don't break lookups
- [x] Table grows correctly (200 entries, all retrievable)
- [x] `find_string` finds by chars+length+hash
- [x] `add_all` copies all entries src → dst
- [x] `remove_white` removes unmarked-key entries
- [x] `mark` marks all keys
- [x] No memory leaks

## Notes

- **Tombstone**: `key == NULL` + `value` non-nil (`ms_bool_val(true)`). Empty: `key == NULL` + `value == ms_nil_val()`.
- **Load factor**: 75% (`MS_TABLE_MAX_LOAD`). Grow when exceeded.
- `ms_table_mark` is placeholder. Real impl receives `MsVM*`/GC context → proper mark calls (T17).
- Internal helpers (`ms_table_find_entry`, `ms_table_adjust_capacity`) are `static` in `table.c`.
