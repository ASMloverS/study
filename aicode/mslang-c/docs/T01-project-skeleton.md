# T01: Project Skeleton & Build System

**Goal:** CMake project → buildable binary + test infra.
**Deps:** None
**Output:** `mslang-c` binary (`--version` prints version); CTest ready.

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `CMakeLists.txt` | Root build, C11, strict warnings |
| Create | `include/ms/common.h` | Type aliases, platform macros |
| Create | `include/ms/consts.h` | Compile-time constants |
| Create | `src/main.c` | Entry point, version print |
| Create | `tests/test_assert.h` | Minimal test macros |
| Create | `tests/CMakeLists.txt` | Test targets |
| Create | `cmake/MslangTesting.cmake` | Test helper module |

## API

```c
// include/ms/common.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef uint8_t  ms_u8;
typedef uint16_t ms_u16;
typedef uint32_t ms_u32;
typedef uint64_t ms_u64;
typedef int8_t   ms_i8;
typedef int32_t  ms_i32;
typedef int64_t  ms_i64;
typedef size_t   ms_sz;

#define MS_UNUSED(x) ((void)(x))

#if defined(__GNUC__) || defined(__clang__)
  #define MS_LIKELY(x)   __builtin_expect(!!(x), 1)
  #define MS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
  #define MS_LIKELY(x)   (x)
  #define MS_UNLIKELY(x) (x)
#endif
```

```c
// include/ms/consts.h
#pragma once
#define MS_STACK_MAX        256
#define MS_FRAMES_MAX        64
#define MS_MAX_UPVALUES     256
#define MS_MAX_CONSTANTS    65536
#define MS_GC_NURSERY_SIZE  (256 * 1024)
#define MS_GC_PROMOTE_AGE   3
#define MS_GC_INCR_WORK     64
#define MS_IC_PIC_SIZE      4
#define MS_SBO_FIELDS       8
#define MS_POOL_SLAB_SIZE   64
#define MS_VERSION          "0.1.0"
```

## Notes

- `CMakeLists.txt`: min 3.20, C11; MSVC `/W4 /WX`; GCC/Clang `-Wall -Wextra -Wpedantic -Werror`
- `test_assert.h`: `TEST_ASSERT(cond)`, `TEST_ASSERT_EQ(a,b)`, `TEST_ASSERT_STR_EQ(a,b)` → print file:line + `exit(1)` on fail
- `MslangTesting.cmake`: `ms_add_test(name src)` → target + CTest registration
- `main.c`: `--version` → print version; else → usage

## Unit Tests

```c
// tests/unit/test_smoke.c
#include "test_assert.h"
#include "ms/common.h"
#include "ms/consts.h"

int main(void) {
    TEST_ASSERT(sizeof(ms_u8) == 1);
    TEST_ASSERT(sizeof(ms_u32) == 4);
    TEST_ASSERT(sizeof(ms_i64) == 8);
    TEST_ASSERT(MS_STACK_MAX == 256);
    TEST_ASSERT(MS_FRAMES_MAX == 64);
    printf("test_smoke: all passed\n");
    return 0;
}
```

No .ms integration tests — VM not yet impl.
