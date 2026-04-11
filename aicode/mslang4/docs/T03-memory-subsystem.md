# T03: Memory Subsystem

**Phase**: 1 · **Deps**: T02 · **Complexity**: low · **Status**: ✅ COMPLETED

## Goal

Centralized allocation wrapper + utility macros. All heap alloc MUST go through `ms_reallocate()` → enables GC tracking + leak detection.

## Files

| File | Purpose |
|------|---------|
| `src/memory.h` | Memory API + macros |
| `src/memory.c` | Implementation |
| `tests/unit/test_memory.c` | Unit tests |

## TDD Cycles

### Cycle 1: ms_reallocate — Alloc + Free

**RED**: `memory.h`/`.c` missing → compile error.

```c
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_alloc_free(void) {
    void* ptr = ms_reallocate(NULL, 0, 100);
    if (ptr == NULL) {
        fprintf(stderr, "FAIL: ms_reallocate(NULL, 0, 100) returned NULL\n");
        exit(1);
    }
    memset(ptr, 0xAB, 100);

    void* ptr2 = ms_reallocate(ptr, 100, 0);
    if (ptr2 != NULL) {
        fprintf(stderr, "FAIL: ms_reallocate(ptr, 100, 0) should return NULL\n");
        exit(1);
    }
}

int main(void) {
    test_alloc_free();
    printf("test_alloc_free passed\n");
    return 0;
}
```

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_memory tests/unit/test_memory.c)
target_include_directories(test_memory PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_memory PRIVATE maple)
add_test(NAME test_memory COMMAND test_memory)
```

**GREEN**: Create `src/memory.h`:
```c
#ifndef MS_MEMORY_H
#define MS_MEMORY_H

#include "common.h"

void* ms_reallocate(void* pointer, size_t oldSize, size_t newSize);

#endif
```

Create `src/memory.c`:
```c
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>

void* ms_reallocate(void* pointer, size_t oldSize, size_t newSize) {
    (void)oldSize;
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "Maple: out of memory\n");
        exit(1);
    }
    return result;
}
```

Semantics: `NULL,0,size` → `malloc`. `ptr,old,new` → `realloc`. `ptr,old,0` → `free`, returns NULL. Exits on OOM. `oldSize` reserved for GC (T17).

**REFACTOR**: `(void)oldSize;` suppresses unused-param warning.

### Cycle 2: ms_reallocate — Grow In-Place

**RED** (validation — Cycle 1 impl already supports this):

```c
static void test_realloc_grows(void) {
    int* arr = (int*)ms_reallocate(NULL, 0, 5 * sizeof(int));
    if (arr == NULL) { fprintf(stderr, "FAIL: initial alloc returned NULL\n"); exit(1); }

    for (int i = 0; i < 5; i++) arr[i] = i * 10;

    arr = (int*)ms_reallocate(arr, 5 * sizeof(int), 10 * sizeof(int));
    if (arr == NULL) { fprintf(stderr, "FAIL: realloc returned NULL\n"); exit(1); }

    for (int i = 0; i < 5; i++) {
        if (arr[i] != i * 10) {
            fprintf(stderr, "FAIL: data not preserved after realloc at index %d\n", i);
            exit(1);
        }
    }

    ms_reallocate(arr, 10 * sizeof(int), 0);
}
```

**GREEN**: no changes. **REFACTOR**: none.

### Cycle 3: MS_ALLOCATE + MS_FREE

**RED**: `MS_ALLOCATE`/`MS_FREE` undeclared → compile error.

```c
static void test_allocate_free_macro(void) {
    int* arr = MS_ALLOCATE(int, 10);
    if (arr == NULL) { fprintf(stderr, "FAIL: MS_ALLOCATE returned NULL\n"); exit(1); }

    for (int i = 0; i < 10; i++) arr[i] = i;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != i) { fprintf(stderr, "FAIL: MS_ALLOCATE data mismatch at %d\n", i); exit(1); }
    }

    MS_FREE(int, arr, 10);
}
```

**GREEN**: Add to `src/memory.h`:
```c
#define MS_ALLOCATE(type, count) \
    (type*)ms_reallocate(NULL, 0, sizeof(type) * (count))
#define MS_FREE(type, pointer, count) \
    ms_reallocate(pointer, sizeof(type) * (count), 0)
```

**REFACTOR**: none.

### Cycle 4: MS_GROW_CAPACITY

**RED**: `MS_GROW_CAPACITY` undeclared → compile error.

```c
static void test_grow_capacity(void) {
    if (MS_GROW_CAPACITY(0) != 8) {
        fprintf(stderr, "FAIL: MS_GROW_CAPACITY(0) expected 8, got %d\n", MS_GROW_CAPACITY(0));
        exit(1);
    }
    if (MS_GROW_CAPACITY(1) != 8) {
        fprintf(stderr, "FAIL: MS_GROW_CAPACITY(1) expected 8\n"); exit(1);
    }
    if (MS_GROW_CAPACITY(7) != 8) {
        fprintf(stderr, "FAIL: MS_GROW_CAPACITY(7) expected 8\n"); exit(1);
    }
    if (MS_GROW_CAPACITY(8) != 16) {
        fprintf(stderr, "FAIL: MS_GROW_CAPACITY(8) expected 16, got %d\n", MS_GROW_CAPACITY(8));
        exit(1);
    }
    if (MS_GROW_CAPACITY(16) != 32) {
        fprintf(stderr, "FAIL: MS_GROW_CAPACITY(16) expected 32\n"); exit(1);
    }
    if (MS_GROW_CAPACITY(100) != 200) {
        fprintf(stderr, "FAIL: MS_GROW_CAPACITY(100) expected 200\n"); exit(1);
    }
}
```

**GREEN**: Add to `src/memory.h`:
```c
#define MS_GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)
```

**REFACTOR**: none.

### Cycle 5: MS_GROW_ARRAY + MS_FREE_ARRAY

**RED**: `MS_GROW_ARRAY` undeclared → compile error.

```c
static void test_grow_array(void) {
    int* arr = MS_ALLOCATE(int, 4);
    for (int i = 0; i < 4; i++) arr[i] = i * 100;

    arr = MS_GROW_ARRAY(int, arr, 4, 8);
    if (arr == NULL) { fprintf(stderr, "FAIL: MS_GROW_ARRAY returned NULL\n"); exit(1); }

    for (int i = 0; i < 4; i++) {
        if (arr[i] != i * 100) {
            fprintf(stderr, "FAIL: MS_GROW_ARRAY data not preserved at %d\n", i); exit(1);
        }
    }

    MS_FREE_ARRAY(int, arr, 8);
}
```

**GREEN**: Add to `src/memory.h`:
```c
#define MS_GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)ms_reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))
#define MS_FREE_ARRAY(type, pointer, oldCount) \
    ms_reallocate(pointer, sizeof(type) * (oldCount), 0)
```

**REFACTOR**: Verify final header matches DESIGN.md §2.14.

## Acceptance Criteria

- [x] `ms_reallocate(NULL, 0, 100)` → non-NULL
- [x] `ms_reallocate(ptr, 100, 200)` → realloc to 200
- [x] `ms_reallocate(ptr, 200, 0)` → free, returns NULL
- [x] `MS_ALLOCATE(int, 10)` / `MS_FREE(int, ptr, 10)` work
- [x] `MS_GROW_CAPACITY(0)==8`, `(8)==16`, `(16)==32`
- [x] All macros compile + work
- [x] No leaks (valgrind/sanitizers)

## Notes

- GC fns (`ms_gc_collect`, `ms_gc_mark_*`) → T17. This task = alloc wrapper only.
- `oldSize` currently unused → GC heap tracking in T17.
- Free all allocs via `MS_FREE`/`MS_FREE_ARRAY`.
