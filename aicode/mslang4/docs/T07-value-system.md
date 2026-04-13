# T07: Value System

**Phase**: 2 — Core Data Types
**Deps**: T02 (Common Definitions), T03 (Memory Subsystem)
**Complexity**: Medium

## Goal

Tagged-union value representation — fundamental VM data type. 16 bytes: type tag + union of `bool`/`double`/`MsObject*`.

## Files

| File | Purpose |
|------|---------|
| `src/value.h` | Value types, inline helpers, `MsValueArray` |
| `src/value.c` | Equality, printing, array ops |

## TDD Cycles

### Cycle 1: Value Constructors + Type Checkers

**RED** — Create `tests/unit/test_value.c`:

```c
#include "value.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static void test_value_constructors(void) {
    assert(ms_is_nil(ms_nil_val()));
    assert(ms_nil_val().type == MS_VAL_NIL);

    MsValue bv = ms_bool_val(true);
    assert(ms_is_bool(bv));
    assert(ms_as_bool(bv) == true);

    MsValue nv = ms_number_val(3.14);
    assert(ms_is_number(nv));
    assert(fabs(ms_as_number(nv) - 3.14) < 1e-10);

    assert(sizeof(MsValue) == 16);
    printf("  test_value_constructors PASSED\n");
}

int main(void) {
    printf("Running value tests...\n");
    test_value_constructors();
    printf("All value tests passed.\n");
    return 0;
}
```

`value.h` doesn't exist → compile error.

**Verify RED**: `gcc -I src -o build/test_value tests/unit/test_value.c src/value.c` → `value.h` not found

**GREEN** — Create `src/value.h`:

```c
#ifndef MS_VALUE_H
#define MS_VALUE_H

#include "common.h"

typedef struct MsObject MsObject;

typedef enum { MS_VAL_NIL, MS_VAL_BOOL, MS_VAL_NUMBER, MS_VAL_OBJ } MsValueType;

typedef struct {
    MsValueType type;
    union { bool boolean; double number; MsObject* obj; };
} MsValue;

static inline MsValue ms_nil_val(void)    { return (MsValue){ .type = MS_VAL_NIL, .number = 0 }; }
static inline MsValue ms_bool_val(bool v) { return (MsValue){ .type = MS_VAL_BOOL, .boolean = v }; }
static inline MsValue ms_number_val(double v) { return (MsValue){ .type = MS_VAL_NUMBER, .number = v }; }
static inline MsValue ms_obj_val(MsObject* o) { return (MsValue){ .type = MS_VAL_OBJ, .obj = o }; }

static inline bool ms_is_nil(MsValue v)    { return v.type == MS_VAL_NIL; }
static inline bool ms_is_bool(MsValue v)   { return v.type == MS_VAL_BOOL; }
static inline bool ms_is_number(MsValue v) { return v.type == MS_VAL_NUMBER; }
static inline bool ms_is_obj(MsValue v)    { return v.type == MS_VAL_OBJ; }

static inline bool ms_as_bool(MsValue v)     { return v.boolean; }
static inline double ms_as_number(MsValue v) { return v.number; }
static inline MsObject* ms_as_obj(MsValue v) { return v.obj; }

#endif
```

Create `src/value.c` as empty TU (just `#include "value.h"`). Inline fns in header sufficient for this cycle.

**Verify GREEN**: `gcc -I src -o build/test_value tests/unit/test_value.c src/value.c && ./build/test_value`

**REFACTOR**: None needed.

---

### Cycle 2: Value Equality

**RED** — Add to `test_value.c`:

```c
static void test_value_equality(void) {
    assert(ms_values_equal(ms_number_val(42), ms_number_val(42)));
    assert(!ms_values_equal(ms_number_val(1), ms_number_val(2)));
    assert(ms_values_equal(ms_bool_val(true), ms_bool_val(true)));
    assert(!ms_values_equal(ms_bool_val(true), ms_bool_val(false)));
    assert(ms_values_equal(ms_nil_val(), ms_nil_val()));
    assert(!ms_values_equal(ms_nil_val(), ms_bool_val(false)));
    printf("  test_value_equality PASSED\n");
}
```

`ms_values_equal` undefined → linker error.

**Verify RED**: link error — `ms_values_equal` undefined

**GREEN** — Declare in `src/value.h`:
```c
bool ms_values_equal(MsValue a, MsValue b);
```

Implement in `src/value.c`:
```c
#include "value.h"
#include <stdbool.h>

bool ms_values_equal(MsValue a, MsValue b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case MS_VAL_NIL:    return true;
        case MS_VAL_BOOL:   return a.boolean == b.boolean;
        case MS_VAL_NUMBER: return a.number == b.number;
        case MS_VAL_OBJ:    return a.obj == b.obj;
    }
    return false;
}
```

Compare type tags first, then union payload. OBJ → pointer equality (interned strings).

**Verify GREEN**: build + run → both tests pass.

**REFACTOR**: None needed.

---

### Cycle 3: Falsey Semantics

**RED** — Add to `test_value.c`:

```c
static void test_falsey(void) {
    assert(ms_is_falsey(ms_nil_val()));
    assert(ms_is_falsey(ms_bool_val(false)));
    assert(!ms_is_falsey(ms_bool_val(true)));
    assert(!ms_is_falsey(ms_number_val(0)));
    assert(!ms_is_falsey(ms_number_val(1)));
    printf("  test_falsey PASSED\n");
}
```

`ms_is_falsey` undeclared → compile error.

**GREEN** — Declare in `src/value.h`:
```c
bool ms_is_falsey(MsValue value);
```

Implement in `src/value.c`:
```c
bool ms_is_falsey(MsValue value) {
    return ms_is_nil(value) || (ms_is_bool(value) && !ms_as_bool(value));
}
```

`nil` and `false` are falsey; everything else (including `0`, empty string) is truthy.

**Verify GREEN**: build + run → all three tests pass.

**REFACTOR**: None needed.

---

### Cycle 4: Value Printing

**RED** — Add to `test_value.c`:

```c
static void test_print_value(void) {
    printf("  Expect 'nil': "); ms_print_value(ms_nil_val()); printf("\n");
    printf("  Expect 'true': "); ms_print_value(ms_bool_val(true)); printf("\n");
    printf("  Expect 'false': "); ms_print_value(ms_bool_val(false)); printf("\n");
    printf("  Expect '42': "); ms_print_value(ms_number_val(42)); printf("\n");
    printf("  Expect '3.14': "); ms_print_value(ms_number_val(3.14)); printf("\n");
    printf("  test_print_value PASSED (visual check)\n");
}
```

`ms_print_value` undeclared → compile error.

**GREEN** — Declare in `src/value.h`:
```c
void ms_print_value(MsValue value);
```

Implement in `src/value.c`:
```c
#include <stdio.h>

void ms_print_value(MsValue value) {
    switch (value.type) {
        case MS_VAL_NIL:    printf("nil"); break;
        case MS_VAL_BOOL:   printf(value.boolean ? "true" : "false"); break;
        case MS_VAL_NUMBER: printf("%.14g", value.number); break;
        case MS_VAL_OBJ:    ms_object_print(value); break;
    }
}
```

`ms_object_print` stubbed via forward decl; implemented in T08. This cycle tests NIL/BOOL/NUMBER only.

**Verify GREEN**: build + run → test passes.

**REFACTOR**: None. `ms_object_print` resolves when T08 linked.

---

### Cycle 5: ValueArray Init/Free/Write

**RED** — Add to `test_value.c`:

```c
static void test_value_array_basic(void) {
    MsValueArray arr;
    ms_value_array_init(&arr);
    assert(arr.values == NULL);
    assert(arr.count == 0);
    assert(arr.capacity == 0);

    ms_value_array_write(&arr, ms_number_val(1));
    ms_value_array_write(&arr, ms_number_val(2));
    ms_value_array_write(&arr, ms_number_val(3));
    assert(arr.count == 3);
    assert(ms_as_number(arr.values[0]) == 1.0);
    assert(ms_as_number(arr.values[1]) == 2.0);
    assert(ms_as_number(arr.values[2]) == 3.0);

    ms_value_array_free(&arr);
    assert(arr.values == NULL);
    assert(arr.count == 0);
    assert(arr.capacity == 0);
    printf("  test_value_array_basic PASSED\n");
}
```

`MsValueArray` etc. undeclared → compile error.

**GREEN** — Add to `src/value.h`:
```c
typedef struct { MsValue* values; int count; int capacity; } MsValueArray;

void ms_value_array_init(MsValueArray* array);
void ms_value_array_free(MsValueArray* array);
void ms_value_array_write(MsValueArray* array, MsValue value);
```

Implement in `src/value.c`:
```c
void ms_value_array_init(MsValueArray* array) {
    array->values = NULL;
    array->count = 0;
    array->capacity = 0;
}

void ms_value_array_free(MsValueArray* array) {
    MS_FREE_ARRAY(MsValue, array->values, array->capacity);
    ms_value_array_init(array);
}

void ms_value_array_write(MsValueArray* array, MsValue value) {
    if (array->capacity < array->count + 1) {
        int old_capacity = array->capacity;
        array->capacity = MS_GROW_CAPACITY(old_capacity);
        array->values = MS_GROW_ARRAY(MsValue, array->values, old_capacity, array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}
```

Grow via `MS_GROW_CAPACITY` + `MS_GROW_ARRAY` when full.

**Verify GREEN**: build + run → all four tests pass.

**REFACTOR**: None needed.

---

### Cycle 6: ValueArray Dynamic Growth

**RED** — Add to `test_value.c`:

```c
static void test_value_array_growth(void) {
    MsValueArray arr;
    ms_value_array_init(&arr);

    for (int i = 0; i < 200; i++) {
        ms_value_array_write(&arr, ms_number_val(i));
    }
    assert(arr.count == 200);
    for (int i = 0; i < 200; i++) {
        assert(ms_as_number(arr.values[i]) == (double)i);
    }

    ms_value_array_free(&arr);
    printf("  test_value_array_growth PASSED\n");
}
```

Uses existing API — should already compile. Fails at runtime if growth broken.

**Verify GREEN**: build + run → all tests pass. Verify no leaks with ASan:
`gcc -fsanitize=address -I src -o build/test_value tests/unit/test_value.c src/value.c && ./build/test_value`

**REFACTOR**: None needed.

## Acceptance Criteria

- [x] `sizeof(MsValue) == 16` on 64-bit
- [x] `ms_nil_val().type == MS_VAL_NIL`
- [x] `ms_bool_val(true).boolean == true`
- [x] `ms_number_val(3.14).number` ≈ 3.14
- [x] `ms_values_equal(ms_number_val(42), ms_number_val(42))` → true
- [x] `ms_values_equal(ms_number_val(1), ms_number_val(2))` → false
- [x] `ms_is_falsey(ms_nil_val())` → true
- [x] `ms_is_falsey(ms_bool_val(false))` → true
- [x] `ms_is_falsey(ms_number_val(0))` → false
- [x] `ms_print_value(ms_number_val(42))` prints `"42"`
- [x] `MsValueArray` init/write/free cycle, no leaks
- [x] `MsValueArray` grows correctly (100+ values)

## Notes

- `ms_print_value` for OBJ delegates to `ms_object_print()` (T08). Until T08 linked, only NIL/BOOL/NUMBER printing testable.
- All inline helpers in header — called frequently throughout VM.
- `ms_values_equal` uses pointer equality for OBJ (strings interned). Relies on VM's string interning table.
