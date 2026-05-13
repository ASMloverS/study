# Task 17: GC — Generational Collection

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Extend GC to generational (young/old), write barriers, remembered set, ObjectPool slab allocator.
**Deps:** T16
**Produces:** Efficient generational GC; short-lived objects quickly reclaimed by minor GC

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/vm_gc.c` | minor/major GC, promotion, remembered set |
| Modify | `src/memory.c` | write_barrier, ObjectPool |
| Modify | `include/ms/memory.h` | write_barrier API, MsObjectPool |
| Modify | `include/ms/vm.h` | Generational GC fields |
| Create | `tests/unit/test_gc_gen.c` | Generational GC tests |

## Key Data Structures / API

```c
// Append to include/ms/memory.h
typedef struct MsPoolSlab {
    struct MsPoolSlab* next;
    int used;
    char data[];  // FAM: obj_size * MS_POOL_SLAB_SIZE
} MsPoolSlab;

typedef struct {
    MsPoolSlab* slabs;
    void* free_list;
    size_t obj_size;
} MsObjectPool;

void  ms_pool_init(MsObjectPool* pool, size_t obj_size);
void* ms_pool_alloc(MsObjectPool* pool);
void  ms_pool_free_obj(MsObjectPool* pool, void* ptr);
void  ms_pool_destroy(MsObjectPool* pool);

// Write barrier: call when old-gen object stores young-gen reference
void ms_write_barrier(MsVM* vm, MsObject* owner, MsValue val);
```

```c
// Append to include/ms/vm.h
MsObject* young_objects;     // young-gen linked list
MsObject* old_objects;       // old-gen linked list
MsObject** remembered_set;   // old-gen objects referencing young-gen objects
int remembered_count;
int remembered_capacity;
size_t young_bytes;          // bytes allocated in nursery
MsObjectPool upvalue_pool;   // ObjUpvalue slab pool
MsObjectPool bound_pool;     // ObjBoundMethod slab pool (T18)
```

## Impl Notes

### Allocation Strategy

New objs → young gen: `obj->generation = 0; obj->age = 0;` → appended to `vm->young_objects`.

### Minor GC (trigger: `young_bytes > MS_GC_NURSERY_SIZE`)

1. mark_roots (same as T16)
2. Mark all objects in `remembered_set`
3. Trace (only gray objects in the young generation)
4. Sweep `young_objects`: unmarked → free; marked → `age++`; if `age >= MS_GC_PROMOTE_AGE` → move to `old_objects`, `generation=1`
5. Clear `remembered_set`
6. Reset `young_bytes = 0`

### Major GC (trigger: old gen too large after multiple minors)

Full mark+sweep: sweeps both `young` + `old`.

### Write Barrier

```c
void ms_write_barrier(MsVM* vm, MsObject* owner, MsValue val) {
    if (owner->generation != 1) return;          // owner must be old
    if (!MS_IS_OBJECT(val)) return;
    MsObject* child = MS_AS_OBJECT(val);
    if (child->generation != 0) return;           // child must be young
    if (owner->in_remembered_set) return;
    owner->in_remembered_set = true;
    MS_ARRAY_PUSH(/*remembered_set dynamic array*/);
}
```

Call after:
- `SETUPVAL`: `ms_write_barrier(vm, (MsObject*)frame->closure, R(A))`
- `SETPROP`: `ms_write_barrier(vm, (MsObject*)instance, value)`
- `SETGLOBAL`: globals always roots; skip generational tracking

### ObjectPool

Slab allocator for small hot objs (`ObjUpvalue`, `ObjBoundMethod`):
- Each slab holds 64 objects
- `free_list`: singly-linked list of freed objects (next ptr stored in-place)
- alloc: take from `free_list`; new slab if empty
- free: return to `free_list`

## C Unit Tests

```c
// tests/unit/test_gc_gen.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_young_gen_collection(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var total = 0\n"
        "var i = 0\n"
        "while (i < 5000) {\n"
        "  var temp = \"x\"\n"
        "  total = total + 1\n"
        "  i = i + 1\n"
        "}\n"
        "print(total)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_young_gen_collection();
    printf("test_gc_gen: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/gc_generational.ms
// Many short-lived objects (nursery pressure)
fun stress() {
  var total = 0
  var i = 0
  while (i < 10000) {
    var s = "item"
    total = total + 1
    i = i + 1
  }
  return total
}
print(stress())
// expect: 10000

// tests/fixtures/gc_promotion.ms
// Long-lived objects should be promoted to old generation
var long_lived = "permanent"
var i = 0
while (i < 5000) {
  var _ = "temp"
  i = i + 1
}
print(long_lived)
// expect: permanent

// tests/fixtures/gc_closure_survival.ms
fun make() {
  var x = "captured"
  return fun() { return x }
}
var f = make()
var i = 0
while (i < 5000) { var _ = "gc_pressure"; i = i + 1 }
print(f())
// expect: captured
```
