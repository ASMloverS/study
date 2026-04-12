# Task 30: Bytecode Serializer and Incremental GC

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** .msc binary serialization/deserialization + auto-caching; incremental marking for major GC.
**Deps:** T17, T28
**Produces:** .msc cache accelerates repeated exec; incremental GC reduces pause time

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/serializer.h` | Serializer API |
| Create | `src/serializer.c` | .msc binary format read/write |
| Modify | `src/vm_gc.c` | Incremental marking |
| Modify | `include/ms/vm.h` | Incremental GC state |
| Create | `tests/unit/test_serializer.c` | Serializer roundtrip tests |

## Key Data Structures / API

```c
// include/ms/serializer.h
#pragma once
#include "ms/object.h"

// .msc file header (16 bytes)
#define MS_MSC_MAGIC  "MSC\0"
#define MS_MSC_VERSION 1

typedef struct {
    char magic[4];     // "MSC\0"
    uint32_t version;  // format version
    uint32_t flags;    // reserved
    uint32_t src_hash; // source FNV-1a hash (validates cache freshness)
} MsMscHeader;

// Serialize ObjFunction to file
bool ms_serialize(MsObjFunction* fn, const char* path, uint32_t src_hash);
// Deserialize; returns NULL if file missing or hash mismatch
MsObjFunction* ms_deserialize(MsVM* vm, const char* path, uint32_t src_hash);

// Auto-caching wrapper
MsObjFunction* ms_compile_cached(MsVM* vm, const char* source,
                                   const char* src_path);
```

```c
// Incremental GC state (append to MsVM)
typedef enum {
    MS_GC_IDLE,
    MS_GC_MARKING,
    MS_GC_SWEEPING,
} MsGcPhase;

// Append to MsVM:
MsGcPhase gc_phase;
MsObject* sweep_cursor;  // current position during SWEEPING phase
```

## Impl Notes

### .msc Binary Format

```
[Header: 16 bytes]
[FunctionCount: u32]
[Functions: serialized in DFS post-order]
  For each function:
    [name_length: u32] [name: bytes]
    [arity: u32] [min_arity: i32] [upvalue_count: u32]
    [max_stack_size: u32] [is_generator: u8]
    [code_count: u32] [code: u32 * code_count]
    [const_count: u32]
    For each constant:
      [type: u8]
      [data: varies by type]
        NIL: (nothing)
        BOOL: [u8]
        INT: [i64]
        NUMBER: [f64]
        STRING: [length: u32] [chars]
        FUNCTION: [index: u32]  (reference to already-serialized function)
    [line_count: u32]
    For each SourceRun:
      [line: u32] [column: u32] [count: u32]
```

### DFS Post-Order

Nested fns serialize first (post-order) → FUNCTION const ref already exists on deserialize.

### Auto-Caching

```c
MsObjFunction* ms_compile_cached(MsVM* vm, const char* source, const char* src_path) {
    uint32_t src_hash = ms_fnv1a(source, strlen(source));
    // Build .msc path: append "c" to src_path (.ms → .msc)
    char msc_path[PATH_MAX];
    snprintf(msc_path, sizeof(msc_path), "%sc", src_path);

    // Try loading from cache
    MsObjFunction* fn = ms_deserialize(vm, msc_path, src_hash);
    if (fn) return fn;

    // Cache invalid: compile and save
    fn = ms_compile(vm, source, src_path, ...);
    if (fn) ms_serialize(fn, msc_path, src_hash);
    return fn;
}
```

### Incremental Marking

Major GC marking → small steps:

```c
void ms_gc_incremental_step(MsVM* vm) {
    switch (vm->gc_phase) {
    case MS_GC_IDLE:
        // Begin marking: mark roots
        mark_roots(vm);
        vm->gc_phase = MS_GC_MARKING;
        break;

    case MS_GC_MARKING:
        // Process MS_GC_INCR_WORK (64) gray objects per step
        for (int i = 0; i < MS_GC_INCR_WORK && vm->gray_count > 0; i++) {
            MsObject* obj = vm->gray_stack[--vm->gray_count];
            blacken_object(vm, obj);
        }
        if (vm->gray_count == 0) {
            // Marking complete; begin sweeping
            ms_table_remove_white(&vm->strings);
            vm->sweep_cursor = vm->objects;
            vm->gc_phase = MS_GC_SWEEPING;
        }
        break;

    case MS_GC_SWEEPING:
        // Sweep MS_GC_INCR_WORK objects per step
        for (int i = 0; i < MS_GC_INCR_WORK && vm->sweep_cursor; i++) {
            MsObject* obj = vm->sweep_cursor;
            vm->sweep_cursor = obj->next;
            if (!obj->is_marked) {
                // Remove from list and free
                // (requires prev pointer or pointer-to-pointer technique)
            } else {
                obj->is_marked = false;
            }
        }
        if (!vm->sweep_cursor) {
            vm->gc_phase = MS_GC_IDLE;
            vm->next_gc = vm->bytes_allocated * 2;
        }
        break;
    }
}
```

In `ms_reallocate`: `gc_phase != IDLE` → call `ms_gc_incremental_step`; `gc_phase == IDLE && bytes_allocated > threshold` → start incremental GC cycle.

### Additional Phase 15 Features

Implement alongside:
- **Operator overloading**: `__add`, `__sub`, `__mul`, `__div`, `__mod`, `__eq`, `__lt`, `__gt`, `__str` — in arithmetic/comparison ops, if operand is ObjInstance, look up the dunder method.
- **Enum declarations**: `enum Color { Red, Green, Blue }` → compile to class with integer-valued static fields.
- **Ternary operator**: `cond ? then : else` → TEST + JMP.
- **for-in iteration**: complete FORITER to support list/map/range iteration.

## C Unit Tests

```c
// tests/unit/test_serializer.c
#include "test_assert.h"
#include "ms/serializer.h"
#include "ms/compiler.h"
#include "ms/vm.h"

static void test_roundtrip(void) {
    MsVM vm; ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    const char* src = "fun add(a,b) { return a + b }\nprint(add(1,2))";
    MsObjFunction* fn = ms_compile(&vm, src, "test.ms", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);

    uint32_t hash = ms_fnv1a(src, (int)strlen(src));
    TEST_ASSERT(ms_serialize(fn, "_test.msc", hash));

    MsObjFunction* fn2 = ms_deserialize(&vm, "_test.msc", hash);
    TEST_ASSERT(fn2 != NULL);
    TEST_ASSERT_EQ(fn2->arity, fn->arity);
    TEST_ASSERT_EQ(fn2->chunk.code_count, fn->chunk.code_count);

    // Wrong hash → returns NULL
    MsObjFunction* fn3 = ms_deserialize(&vm, "_test.msc", hash + 1);
    TEST_ASSERT(fn3 == NULL);

    remove("_test.msc");
    ms_vm_free(&vm);
}

int main(void) {
    test_roundtrip();
    printf("test_serializer: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/operator_overload.ms
class Vec {
  init(x, y) {
    this.x = x
    this.y = y
  }
  __add(other) {
    return Vec(this.x + other.x, this.y + other.y)
  }
  __str() {
    return "(" + this.x + ", " + this.y + ")"
  }
}
var a = Vec(1, 2)
var b = Vec(3, 4)
var c = a + b
print(c)
// expect: (4, 6)

// tests/fixtures/enum.ms
enum Color { Red, Green, Blue }
print(Color.Red)
// expect: 0
print(Color.Green)
// expect: 1
print(Color.Blue)
// expect: 2

// tests/fixtures/ternary.ms
var x = 10
print(x > 5 ? "big" : "small")
// expect: big
print(x < 5 ? "big" : "small")
// expect: small

// tests/fixtures/for_in.ms
for (var item in [10, 20, 30]) {
  print(item)
}
// expect: 10
// expect: 20
// expect: 30

for (var key in {"a": 1, "b": 2}) {
  print(key)
}
// expect: a
// expect: b

// tests/fixtures/serializer_cache.ms
// Validates .msc caching: run twice, second run loads from cache
// (requires test runner support; this script only verifies correct semantics)
fun compute() {
  var sum = 0
  for (var i = 0; i < 100; i = i + 1) {
    sum = sum + i
  }
  return sum
}
print(compute())
// expect: 4950
```
