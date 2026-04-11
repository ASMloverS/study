# T02: Common Definitions

**Phase**: 1 · **Deps**: T01 · **Complexity**: low · **Status**: ✅ COMPLETED

## Goal

Shared constants, `MsResult`, debug macros → foundational header included everywhere.

## Files

| File | Action |
|------|--------|
| `src/common.h` | Fill definitions |
| `tests/unit/test_common.c` | Unit tests |
| `tests/CMakeLists.txt` | Add `test_common` target |

## TDD Cycles

### Cycle 1: Include Guards + Standard Includes

**RED**: Create test that `#include "common.h"` — `common.h` is empty placeholder from T01, test infra not yet in `tests/CMakeLists.txt`.

```c
#include "common.h"
#include <stdio.h>

static void test_common_includes(void) {
    printf("common.h included successfully\n");
}

int main(void) {
    test_common_includes();
    return 0;
}
```

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_common tests/unit/test_common.c)
target_include_directories(test_common PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_common COMMAND test_common)
```

Verify: `cmake --build build && ctest --test-dir build -R test_common` → passes trivially.

**GREEN**: Fill `src/common.h`:
```c
#ifndef MS_COMMON_H
#define MS_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#endif
```

**REFACTOR**: none.

### Cycle 2: Compile-Time Constants

**RED**: Test asserts on undefined constants → compile error.

```c
static void test_stack_max(void) {
    if (MS_STACK_MAX != 256) {
        fprintf(stderr, "FAIL: MS_STACK_MAX expected 256, got %d\n", MS_STACK_MAX);
        exit(1);
    }
}

static void test_frames_max(void) {
    if (MS_FRAMES_MAX != 64) {
        fprintf(stderr, "FAIL: MS_FRAMES_MAX expected 64, got %d\n", MS_FRAMES_MAX);
        exit(1);
    }
}

static void test_gc_heap_grow_factor(void) {
    if (MS_GC_HEAP_GROW_FACTOR != 2) {
        fprintf(stderr, "FAIL: MS_GC_HEAP_GROW_FACTOR expected 2, got %d\n", MS_GC_HEAP_GROW_FACTOR);
        exit(1);
    }
}

static void test_max_locals(void) {
    if (MS_MAX_LOCALS != 256) {
        fprintf(stderr, "FAIL: MS_MAX_LOCALS expected 256, got %d\n", MS_MAX_LOCALS);
        exit(1);
    }
}

static void test_max_upvalues(void) {
    if (MS_MAX_UPVALUES != 256) {
        fprintf(stderr, "FAIL: MS_MAX_UPVALUES expected 256, got %d\n", MS_MAX_UPVALUES);
        exit(1);
    }
}

static void test_table_max_load(void) {
    if (MS_TABLE_MAX_LOAD != 0.75) {
        fprintf(stderr, "FAIL: MS_TABLE_MAX_LOAD expected 0.75, got %f\n", MS_TABLE_MAX_LOAD);
        exit(1);
    }
}
```

**GREEN**: Add to `src/common.h`:
```c
#define MS_STACK_MAX    256
#define MS_FRAMES_MAX   64
#define MS_GC_HEAP_GROW_FACTOR 2
#define MS_MAX_LOCALS   256
#define MS_MAX_UPVALUES 256
#define MS_TABLE_MAX_LOAD 0.75
```

**REFACTOR**: none.

### Cycle 3: MsResult Enum

**RED**: Test references `MsResult` / `MS_OK` → undeclared.

```c
static void test_result_values(void) {
    if (MS_OK != 0) {
        fprintf(stderr, "FAIL: MS_OK expected 0, got %d\n", MS_OK);
        exit(1);
    }
    if (MS_COMPILE_ERROR != 1) {
        fprintf(stderr, "FAIL: MS_COMPILE_ERROR expected 1, got %d\n", MS_COMPILE_ERROR);
        exit(1);
    }
    if (MS_RUNTIME_ERROR != 2) {
        fprintf(stderr, "FAIL: MS_RUNTIME_ERROR expected 2, got %d\n", MS_RUNTIME_ERROR);
        exit(1);
    }
}

static void test_result_is_enum(void) {
    MsResult result = MS_OK;
    (void)result;
}
```

**GREEN**: Add to `src/common.h`:
```c
typedef enum { MS_OK, MS_COMPILE_ERROR, MS_RUNTIME_ERROR } MsResult;
```

Universal return type for fallible ops. Values sequential from 0.

**REFACTOR**: none.

### Cycle 4: Debug Macros

**RED**: Test references `MS_DEBUG_LOG_GC_EXECUTE` → undeclared.

```c
static int debug_gc_counter;

static void test_debug_macros_off(void) {
    debug_gc_counter = 0;
    MS_DEBUG_LOG_GC_EXECUTE(debug_gc_counter++);
    if (debug_gc_counter != 0) {
        fprintf(stderr, "FAIL: MS_DEBUG_LOG_GC_EXECUTE should be no-op when flag not defined\n");
        exit(1);
    }

    MS_DEBUG_STRESS_GC_EXECUTE(debug_gc_counter++);
    if (debug_gc_counter != 0) {
        fprintf(stderr, "FAIL: MS_DEBUG_STRESS_GC_EXECUTE should be no-op when flag not defined\n");
        exit(1);
    }

    MS_DEBUG_TRACE_EXECUTE(debug_gc_counter++);
    if (debug_gc_counter != 0) {
        fprintf(stderr, "FAIL: MS_DEBUG_TRACE_EXECUTE should be no-op when flag not defined\n");
        exit(1);
    }
}
```

**GREEN**: Add to `src/common.h`:
```c
#ifdef MS_DEBUG_LOG_GC
  #define MS_DEBUG_LOG_GC_EXECUTE(code) code
#else
  #define MS_DEBUG_LOG_GC_EXECUTE(code)
#endif

#ifdef MS_DEBUG_STRESS_GC
  #define MS_DEBUG_STRESS_GC_EXECUTE(code) code
#else
  #define MS_DEBUG_STRESS_GC_EXECUTE(code)
#endif

#ifdef MS_DEBUG_TRACE_EXECUTION
  #define MS_DEBUG_TRACE_EXECUTE(code) code
#else
  #define MS_DEBUG_TRACE_EXECUTE(code)
#endif
```

Execute-pattern: `code` arg only expanded when corresponding flag defined. No-op in release.

**REFACTOR**: Optional — verify with `-DMS_DEBUG_LOG_GC` build that macro expands correctly.

## Acceptance Criteria

- [x] `common.h` compiles standalone
- [x] All constants defined with correct values
- [x] `MsResult` has exactly 3 values
- [x] Debug macros → no-op without flag, expand `code` with flag
- [x] Zero warnings with strict flags

## Notes

- `common.h` header-only — no `.c` file.
- Constants = compile-time for fixed-stack alloc.
- Debug macros → GC (T17), VM tracing (T14).
