# T19: List/Array Support

**Phase**: 10 · **Deps**: T15 (VM Core), T17 (Garbage Collection) · **Complexity**: Medium

## Goal

List/array support: literal `[1, 2, 3]`, subscript `list[0]`, subscript assignment `list[1] = 10`. `MsList` object with GC integration.

## Files

| File | Changes |
|------|---------|
| `src/object.h` | `MsList` struct, macros |
| `src/object.c` | `ms_list_new`, `ms_list_append`, `ms_list_get`, `ms_list_set`, `ms_list_length`, free/print |
| `src/compiler.c` | `OP_BUILD_LIST`, subscript get/set compilation |
| `src/vm.c` | `OP_BUILD_LIST`, `OP_GET_SUBSCRIPT`, `OP_SET_SUBSCRIPT` dispatch |

## TDD Cycles

### Cycle 1: MsList Object — Creation & Append

**RED**: `ms_list_new`, `ms_list_append`, `ms_list_get` undefined → link error.

- `test_list_create_and_append`: `ms_list_new()` → append 3 values → count == 3, elements correct
- `test_list_empty_create`: empty list → count == 0, capacity == 0

**GREEN**:
- `src/object.h`: add `MS_OBJ_LIST` to enum. Struct:
  ```c
  typedef struct {
      MsObject obj;
      int count;
      int capacity;
      MsValue* elements;
  } MsList;
  ```
  Macros: `MS_LIST_VAL(obj)`, `MS_AS_LIST(val)`, `MS_IS_LIST(val)`.
  Decls: `ms_list_new(vm)`, `ms_list_append(vm, list, value)`, `ms_list_get(list, index)`, `ms_list_length(list)`.
- `src/object.c`: `ms_list_new()` → `ms_alloc_object()`, elements=NULL, count/capacity=0. `ms_list_append()` → grow via `MS_GROW_CAPACITY`/`ms_reallocate()`, append. `ms_list_get()` → return `elements[index]`. `ms_list_length()` → return `count`.

**Verify GREEN**: `cmake --build build && build\test_list`

**REFACTOR**: Extract dynamic array growth into helper macro if reused.

### Cycle 2: MsList — Set & Bounds Checking

**RED**: `ms_list_set` undefined → link error.

- `test_list_set`: append values → `ms_list_set(idx 1)` → new value correct
- `test_list_get_out_of_bounds`: `ms_list_get(list, 5)` → error/sentinel
- `test_list_set_out_of_bounds`: `ms_list_set(list, 10, val)` → error

**GREEN**:
- `ms_list_set(list, index, value)`: bounds check (index >= count or < 0 → `MS_RESULT_RUNTIME_ERROR`), set element
- Update `ms_list_get()`: bounds check → runtime error on out of range

**Verify GREEN**: `cmake --build build && build\test_list`

**REFACTOR**: Bounds check messages include index and list length.

### Cycle 3: MsList — GC Integration (Free & Mark)

**RED**: `ms_object_free()`/`ms_gc_blacken_object()` don't handle `MS_OBJ_LIST` → crash/leak.

- `test_list_gc_mark`: list with GC-managed string → mark phase → string marked reachable
- `test_list_free`: list + values → `ms_object_free()` → no crash/leak

**GREEN**:
- `ms_object_free()`: `MS_OBJ_LIST` → `MS_FREE_ARRAY(MsValue, elements, capacity)` → free MsList
- `ms_gc_blacken_object()`: `MS_OBJ_LIST` → iterate `elements[0..count-1]` → `ms_gc_mark_value()` each
- `ms_object_print()`: `MS_OBJ_LIST` → `[` + comma-separated elements + `]`

**Verify GREEN**: `cmake --build build && build\test_list` — no leaks

**REFACTOR**: `ms_print_value()` handles list comma separation properly.

### Cycle 4: Compiler — List Literal Syntax

**RED**: `[` not recognized → compile error.

- `test_list_literal.ms`: `var list = [1, 2, 3]\nprint list` → `[1, 2, 3]`
- `test_list_empty_literal.ms`: `var list = []\nprint list` → `[]`

**GREEN**:
- `src/compiler.c`: add `OP_BUILD_LIST`. On `[` → compile each element → emit `OP_CONSTANT` each → on `]` emit `OP_BUILD_LIST(count)`
- `src/vm.c`: `OP_BUILD_LIST` → pop `count` values → `ms_list_new()` → `ms_list_append()` each (order preserved) → push list

**Verify GREEN**: `build\maple tests\integration\test_list_literal.ms` → `[1, 2, 3]`

**REFACTOR**: First element = index 0.

### Cycle 5: Subscript Get Access

**RED**: `list[0]` → parse error.

- `test_subscript_get.ms`: `var list = [10, 20, 30]\nprint list[0]\nprint list[2]` → "10" then "30"
- `test_nested_subscript.ms`: `var m = [[1,2],[3,4]]\nprint m[0][1]` → "2"

**GREEN**:
- `src/compiler.c`: add `OP_GET_SUBSCRIPT`. After primary, `[` → compile index expr → emit `OP_GET_SUBSCRIPT`. Allow chaining.
- `src/vm.c`: `OP_GET_SUBSCRIPT` → pop index + object → list + number → `ms_list_get()` → push result; else error

**Verify GREEN**: `build\maple tests\integration\test_subscript_get.ms` → "10" then "30"

**REFACTOR**: Subscript works as LHS context for assignment (prep next cycle).

### Cycle 6: Subscript Assignment

**RED**: `list[1] = 10` → parse error / not assignable.

- `test_subscript_set.ms`: `var list = [1, 2, 3]\nlist[1] = 10\nprint list[1]` → "10"
- `test_subscript_runtime_error.ms`: `print list[10]` → runtime error (out of bounds)
- `test_subscript_type_error.ms`: `print list["hello"]` → runtime error (non-number index)

**GREEN**:
- `src/compiler.c`: add `OP_SET_SUBSCRIPT`. Assignment with subscript LHS: compile object → compile index → compile RHS → emit `OP_SET_SUBSCRIPT`
- `src/vm.c`: `OP_SET_SUBSCRIPT` → pop value + index + object → list + number → `ms_list_set()` → push value; else error
- Runtime errors: out-of-bounds, non-number index

**Verify GREEN**: `build\maple tests\integration\test_subscript_set.ms` → "10"

**REFACTOR**: Consolidate subscript type checking into shared helper.

## Acceptance Criteria

- [ ] `var list = [1, 2, 3]\nprint list` → "[1, 2, 3]"
- [ ] `print [1,2,3][0]` → "1"
- [ ] `var list = [1,2,3]\nlist[1] = 10\nprint list[1]` → "10"
- [ ] `var list = []\nprint list` → "[]"
- [ ] Out-of-bounds → runtime error
- [ ] Non-number subscript → runtime error
- [ ] Lists GC-managed (no leaks)
- [ ] Nested: `var m = [[1,2],[3,4]]\nprint m[0][1]` → "2"

## Notes

- MsList: dynamic array (`MsValue* elements`), `MS_GROW_CAPACITY`/`ms_reallocate()` growth pattern
- All list values marked during GC
- Subscript compiles as postfix `[expr]` — similar to call syntax in parser
