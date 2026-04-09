# Task 16: GC — Basic Mark-and-Sweep

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement tri-color mark-and-sweep garbage collector with root marking, gray stack tracing, and sweep.
**Dependencies:** T14
**Produces:** Automatic memory reclamation; long-running scripts don't exhaust memory

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `src/vm_gc.c` | GC mark/trace/sweep implementation |
| Modify | `src/memory.c` | `ms_reallocate` triggers GC |
| Modify | `include/ms/memory.h` | GC API: mark_object, mark_value, collect |
| Modify | `include/ms/vm.h` | Add `gray_stack` and other GC fields |
| Create | `tests/unit/test_gc.c` | GC tests |

## Key Data Structures / API

```c
// Append to include/ms/memory.h
void ms_gc_collect(MsVM* vm);
void ms_mark_object(MsVM* vm, MsObject* obj);
void ms_mark_value(MsVM* vm, MsValue val);
void ms_mark_table(MsVM* vm, MsTable* table);
```

GC fields needed by `MsVM` (pre-declared in T13):
```c
MsObject** gray_stack;
int gray_count;
int gray_capacity;
```

## Implementation Notes

### Tri-Color Mark-and-Sweep

```
Phase 1: mark_roots   — white → gray (push to gray_stack)
Phase 2: trace        — gray  → black (pop from gray_stack, trace children)
Phase 3: sweep        — free all still-white objects
```

### Root Marking

```c
static void mark_roots(MsVM* vm) {
    // 1. Stack values
    for (MsValue* slot = vm->stack; slot < vm->stack_top; slot++)
        ms_mark_value(vm, *slot);
    // 2. Call frame closures
    for (int i = 0; i < vm->frame_count; i++)
        ms_mark_object(vm, (MsObject*)vm->frames[i].closure);
    // 3. Open upvalues
    for (MsObjUpvalue* uv = vm->open_upvalues; uv; uv = uv->next)
        ms_mark_object(vm, (MsObject*)uv);
    // 4. Globals table
    ms_mark_table(vm, &vm->globals);
    // 5. Compiler roots (GC-safe during compilation)
    if (vm->compiler) mark_compiler_roots(vm);
    // 6. Special strings
    if (vm->init_string) ms_mark_object(vm, (MsObject*)vm->init_string);
}
```

### ms_mark_object

```c
void ms_mark_object(MsVM* vm, MsObject* obj) {
    if (obj == NULL || obj->is_marked) return;
    obj->is_marked = true;
    // Push to gray stack (use raw realloc to avoid recursive GC trigger)
    if (vm->gray_count >= vm->gray_capacity) {
        vm->gray_capacity = vm->gray_capacity < 8 ? 8 : vm->gray_capacity * 2;
        vm->gray_stack = realloc(vm->gray_stack,
                                  sizeof(MsObject*) * (size_t)vm->gray_capacity);
    }
    vm->gray_stack[vm->gray_count++] = obj;
}
```

### blacken_object (trace children)

Traces references by object type:
- STRING: no child references
- FUNCTION: trace name, trace objects in constant pool
- CLOSURE: trace function, trace each upvalue
- UPVALUE: trace closed value
- NATIVE: trace name

### sweep

```c
static void sweep(MsVM* vm) {
    MsObject** obj = &vm->objects;
    while (*obj) {
        if ((*obj)->is_marked) {
            (*obj)->is_marked = false;  // reset for next cycle
            obj = &(*obj)->next;
        } else {
            MsObject* dead = *obj;
            *obj = dead->next;
            ms_object_free(vm, dead);
        }
    }
}
```

### GC Trigger

In `ms_reallocate`: `if (vm->bytes_allocated > vm->next_gc) ms_gc_collect(vm);`
After collection: `vm->next_gc = vm->bytes_allocated * 2;`
Initial `next_gc` = 1 MB.

### Intern Table Cleanup

Call `ms_table_remove_white(&vm->strings)` before sweep — removes unmarked interned strings to prevent dangling pointers.

## C Unit Tests

```c
// tests/unit/test_gc.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_gc_runs_without_crash(void) {
    MsVM vm;
    ms_vm_init(&vm);
    ms_vm_interpret(&vm,
        "var i = 0\n"
        "while (i < 500) {\n"
        "  var s = \"temp\" + \"garbage\"\n"
        "  i = i + 1\n"
        "}", "<test>");
    ms_vm_free(&vm);
}

static void test_gc_preserves_live_objects(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var kept = \"alive\"\n"
        "var i = 0\n"
        "while (i < 500) { var _ = \"trash\"\n i = i + 1 }\n"
        "print(kept)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_gc_runs_without_crash();
    test_gc_preserves_live_objects();
    printf("test_gc: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/gc_basic.ms
fun make_garbage() {
  var i = 0
  while (i < 1000) {
    var s = "temp" + "garbage"
    i = i + 1
  }
  return "done"
}
print(make_garbage())
// expect: done

// tests/fixtures/gc_closures.ms
fun make_closure() {
  var data = "kept_alive"
  return fun() { return data }
}
var fn = make_closure()
var i = 0
while (i < 500) { var _ = "junk"; i = i + 1 }
print(fn())
// expect: kept_alive
```
