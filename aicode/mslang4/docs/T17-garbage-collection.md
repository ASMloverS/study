# T17: Garbage Collection

**Phase**: 8 · **Deps**: T16 (Functions & Closures) · **Complexity**: High

## Goal

Mark-and-sweep GC with tri-color marking (gray worklist). GC triggers on allocation. Debug/stress modes.

## Files

| File | Changes |
|------|---------|
| `src/memory.h` | GC decls, `ms_alloc_object`, `ms_free_objects` |
| `src/memory.c` | `ms_gc_collect`, mark, trace, sweep, `ms_alloc_object`, `ms_free_objects` |
| `src/object.c` | `ms_gc_blacken_object` per object type |
| `src/vm.c` | GC trigger on alloc, compiler root marking |

## TDD Cycles

### Cycle 1: GC Infrastructure — ms_alloc_object & Memory Tracking

**RED**: `ms_alloc_object` undefined, `bytesAllocated` untracked.

- `test_vm_bytes_allocated()`: init VM → `bytesAllocated == 0`; alloc string → `> 0`; free VM → no leak
- `test_alloc_object_linked()`: alloc 3 objects → `vm->objects` has all 3; free VM → no leak
- `test_alloc_object_fields()`: alloc → type correct, `isMarked == false`, `next` = previous head
- `test_alloc_tracks_size()`: alloc different types → `bytesAllocated` increments by correct sizes

**GREEN**:
- `src/memory.h`:
  - `ms_alloc_object(vm, size, type)` — macro/inline wrapping alloc + GC
  - `ms_free_objects(vm)` — free all objects in linked list
  - `MS_GC_HEAP_GROW_FACTOR` (2)
  - `MS_DEBUG_LOG_GC`, `MS_DEBUG_STRESS_GC`
- `src/memory.c`:
  - `ms_alloc_object`: `bytesAllocated += size` → `ms_reallocate(NULL, 0, size)` → set type/next/isMarked → link into `vm->objects` → if `STRESS_GC` or `bytesAllocated > nextGC` → `ms_gc_collect(vm)` → return obj
  - `ms_free_objects`: walk `vm->objects`, call `ms_object_free()` each
- Update all `ms_*_new()` → use `ms_alloc_object()` not raw `ms_reallocate()`

**Verify GREEN**: `cmake --build build && ./build/test_gc`

**REFACTOR**: All object creation → `ms_alloc_object()`.

### Cycle 2: Mark Phase — Root Marking

**RED**: Objects on stack/globals freed after collection.

- `test_mark_stack_values()`: push values → collect → stack values still valid
- `test_mark_globals()`: global → string → collect → string accessible
- `test_mark_call_frames()`: frame with closure → collect → closure + function valid
- `test_mark_open_upvalues()`: capture upvalue → collect → upvalue valid
- `test_mark_compiler_roots()`: during compilation → collect → constants + function survive
- `test_mark_module_table()`: module in `vm->modules` → collect → module survives

**GREEN**:
- `ms_gc_mark_roots(vm)`:
  1. Stack: iterate `vm.stack` → `vm.stackTop`, `ms_gc_mark_value()` each
  2. Call frames: mark each closure + its upvalues
  3. Globals: `ms_table_mark(&vm->globals)`
  4. Open upvalues: walk `vm->openUpvalues`, mark each
  5. Compiler roots: `vm->compiler != NULL` → `ms_compiler_mark_roots(vm->compiler)`
  6. Strings table: `ms_table_mark(&vm->strings)`
  7. Modules table: `ms_table_mark(&vm->modules)`
- `ms_gc_mark_value(v)`: object → `ms_gc_mark_object(obj)`
- `ms_gc_mark_object(obj)`: NULL or marked → return; set `isMarked = true`; push gray stack
- `ms_gray_stack_push(vm, obj)`: grow if needed, push, `grayCount++`
- `ms_table_mark(table)`: iterate entries, mark values + string keys

**Verify GREEN**: `cmake --build build && ./build/test_gc`

**REFACTOR**: Gray stack growth → `ms_reallocate()`, tracked in `bytesAllocated`.

### Cycle 3: Trace Phase — Gray Worklist Processing

**RED**: Child objects freed — trace not implemented.

- `test_trace_closure_references()`: closure → function → constants → collect → all survive
- `test_trace_class_references()`: class with methods → mark → method closures survive
- `test_trace_upvalue_closed()`: close upvalue → mark → closed value survives
- `test_trace_nested()`: chain of references → mark head → entire chain survives
- `test_gray_stack_empty()`: after trace → `grayCount == 0`

**GREEN**:
- `src/object.c` — `ms_gc_blacken_object(vm, obj)`, switch on type:
  - `MS_OBJ_STRING`: nothing
  - `MS_OBJ_FUNCTION`: mark name + iterate chunk constants
  - `MS_OBJ_CLOSURE`: mark function + iterate upvalues
  - `MS_OBJ_UPVALUE`: mark `closed`
  - `MS_OBJ_NATIVE`: mark name
  - Stubs: `CLASS`, `INSTANCE`, `BOUND_METHOD`, `MODULE`, `LIST` (T18)
- `src/memory.c` — `ms_gc_trace_references(vm)`: while `grayCount > 0` → pop → `ms_gc_blacken_object` → repeat

**Verify GREEN**: `cmake --build build && ./build/test_gc`

**REFACTOR**: `ms_gc_blacken_object` extensible for new types.

### Cycle 4: Sweep Phase & Collection Cycle

**RED**: Unreachable objects not freed.

- `test_sweep_unreachable()`: alloc, remove refs, collect → `bytesAllocated` decreases
- `test_sweep_keeps_reachable()`: keep some on stack → collect → survive
- `test_full_gc_cycle()`: `ms_gc_collect()` → mark→trace→sweep in order
- `test_next_gc_adjustment()`: after collect → `nextGC == bytesAllocated * GROW_FACTOR`
- `test_objects_list_after_sweep()`: 5 alloc, keep 2 → collect → list has 2
- `test_is_marked_reset()`: after sweep → surviving objects `isMarked == false`

**GREEN**:
- `ms_gc_sweep(vm)`: pointer-to-pointer walk `vm->objects`:
  - marked → reset `isMarked`, advance
  - unmarked → unlink, `ms_object_free()`
- `ms_gc_collect(vm)`:
  1. `before = bytesAllocated`
  2. `ms_gc_mark_roots`
  3. `ms_gc_trace_references`
  4. `ms_table_remove_white(&vm->strings)`
  5. `ms_gc_sweep`
  6. `nextGC = bytesAllocated * MS_GC_HEAP_GROW_FACTOR`
  7. Debug log if enabled
- `ms_table_remove_white`: remove entries where key `isMarked == false`

**Verify GREEN**: `cmake --build build && ./build/test_gc`

**REFACTOR**: Sweep edge cases — empty list, all alive, all dead.

### Cycle 5: GC Integration with VM Operations

**RED**: GC triggers at wrong time, frees live objects, or crashes.

- `test_gc_triggered_on_threshold()`: alloc past `nextGC` → auto-trigger
- `test_gc_during_compilation()`: many constants → GC doesn't free compiler objects
- `test_gc_during_execution()`: many temp strings → execution correct
- `test_gc_with_closures()`: closures + upvalues + GC → all valid
- `test_gc_repeated_cycles()`: multiple cycles → objects survive across cycles
- `test_no_double_free()`: alloc, collect, collect again → no double-free

**GREEN**:
- `ms_alloc_object` trigger logic: `STRESS_GC` → every alloc; else `bytesAllocated > nextGC`
- `isCollecting` guard → prevent re-entrancy
- `ms_compiler_mark_roots(compiler)`: walk state chain, mark each function + constants
- `ms_vm_interpret()`: set `vm->compiler` before compile, clear after
- `ms_gc_mark_value`: nil/bool/number → no-op; object → mark

**Verify GREEN**: `cmake --build build && ./build/test_gc`

**REFACTOR**: Add `isCollecting` flag to VM.

### Cycle 6: Stress Testing & Sanitizer Verification

**RED**: Build with `MS_DEBUG_STRESS_GC` + ASan/UBSan → potential errors.

- `test_stress_basic_program()`: `"var x = 1 + 2\nprint x"` under stress → "3"
- `test_stress_functions()`: `fibonacci(10)` → "55"
- `test_stress_closures()`: closure counter → correct
- `test_stress_string_ops()`: heavy concat → correct

**GREEN**: Fix bugs exposed:
- Missing marks in `ms_gc_blacken_object`
- Gray stack management issues
- All `MsValue` locations holding objects → proper root marking
- `ms_vm_free()` → `ms_free_objects()` + free gray stack → ASan clean

**Verify GREEN**: `cmake --build build && ./build/test_gc` — no sanitizer errors

**REFACTOR**: Final review — every reference traced.

## Acceptance Criteria

- [ ] GC collects unreachable objects
- [ ] Reachable objects survive collection
- [ ] `bytesAllocated` bounded under repeated allocation
- [ ] No use-after-free
- [ ] String intern table cleaned of dead strings
- [ ] Stress GC mode (`MS_DEBUG_STRESS_GC`) works
- [ ] GC log (`MS_DEBUG_LOG_GC`) works
- [ ] No leaks: init → run → free → clean ASan/valgrind
- [ ] No noticeable pauses on small programs

## Notes

- Lifecycle: trigger → mark roots → trace refs → remove white strings → sweep → adjust threshold
- `ms_gc_blacken_object`: String(nothing), Function(name+constants), Closure(function+upvalues), Upvalue(closed), Native(name), Class/Instance/BoundMethod/Module/List (T18)
- Gray stack: `MsObject** grayStack` dynamic array, grows during trace
- `MS_GC_HEAP_GROW_FACTOR` default 2x
- Debug: `MS_DEBUG_LOG_GC` → print activity; `MS_DEBUG_STRESS_GC` → force collect every alloc
