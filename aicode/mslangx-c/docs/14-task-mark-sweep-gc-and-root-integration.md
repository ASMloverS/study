# Task 14 - Mark-Sweep GC and Root Integration

## Goal

Replace ad-hoc heap lifetime management with one non-moving mark-sweep GC that
covers the full runtime root set.

## Design Links

- GC roots, runtime heap boundaries, and memory model:
  [../mslangc-design.md](../mslangc-design.md)
- Repository rules: [../AGENTS.md](../AGENTS.md)

## Dependencies

1. Task 03
2. Task 10
3. Task 11
4. Task 12
5. Task 13

## Scope

1. Implement a non-moving mark-sweep collector.
2. Maintain the object list and mark bits.
3. Integrate roots from stack values, call frames, open upvalues, current
   module references, module cache, interned strings, builtin/native registries,
   and temporary roots.
4. Add a temporary-root mechanism for compile-to-runtime transitions.
5. Add GC stress tests and basic observability.

## Implementation Boundaries

1. This task owns GC correctness, not GC performance tuning.
2. Compile-time arena memory must stay outside the GC domain.
3. Existing runtime semantics must remain unchanged when GC is enabled.
4. Any optional stress mode should improve determinism for tests rather than act
   as a production feature.

## File Ownership

1. GC state, object list, and collector code under `include/ms/runtime/` and
   `src/runtime/`
2. GC root registration touchpoints in VM and allocators
3. GC unit tests under `tests/unit/`
4. GC stress `.ms` scripts under `tests/stress/gc/`

## Diagnostics and Observability Contract

1. Runtime allocation failures remain `phase=runtime` with `MS4xxx`.
2. Module-init failures triggered during import continue to surface as
   `MS5004`.
3. Expose minimal GC counters such as allocation count, free count, and
   collection count for tests.
4. If a `--gc-stress` mode is added, keep it deterministic and test-only.

## Execution Breakdown

Every subtask must start with failing tests, then the minimal implementation,
then the focused verification command listed for that subtask.

Use these status markers consistently in this document:

- `TODO`: not started
- `DOING`: in progress
- `BLOCKED`: waiting on another subtask or prerequisite fix
- `DONE`: completed and verified

| Subtask | Status | Depends on | Summary |
| --- | --- | --- | --- |
| 14.1 | `DONE` | Task 03 | GC state, object list, mark bits, and allocation counters |
| 14.2 | `DONE` | 14.1 | Root traversal for stack values, call frames, and open upvalues |
| 14.3 | `DONE` | 14.2, Task 13 | Root traversal for current module, module cache, interned strings, and builtin/native registries |
| 14.4 | `DONE` | 14.2, 14.3 | Temporary roots for compile-to-runtime and module-loading transitions |
| 14.5 | `DONE` | 14.2, 14.3, 14.4 | Sweep/reclaim integration and unreachable-object tests |
| 14.6 | `DONE` | 14.5 | GC stress coverage, observability checks, and full regression gate |

### Subtask 14.1 - GC state, object list, mark bits, and allocation counters

**Status:** `DONE`

**Depends on:** Task 03 runtime object and allocator support.

**Deliverables**

1. Add explicit GC state to the runtime so the collector can own the object
   list, mark phase bookkeeping, and collection counters.
2. Attach mark bits and collector-owned reachability metadata to every
   GC-managed runtime object.
3. Make allocations register new objects with the collector before they become
   visible to the rest of the runtime.
4. Keep the compile-time arena outside the GC-managed heap.

**Tests to add or update**

1. Create `tests/unit/gc_test.c` for object-registration and counter
   assertions.
2. Add a unit test that allocates a small object graph and checks that the
   object list and counters reflect the expected state before collection.
3. Register the new unit target as `gc.unit` in `tests/unit/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "gc\.unit|runtime_core"
```

**Done when**

1. The runtime can track allocated objects in a collector-owned list.
2. Allocation and collection counters are observable in tests.
3. GC metadata exists without changing user-visible language behavior.

**Implementation summary**

1. `MsVM` now carries explicit `MsGCState` storage for the collector-owned list
   and allocation / free / collection counters.
2. `ms_vm_gc_track_object()` links GC-managed runtime objects into the
   collector list before they are exposed through runtime state.
3. `tests/unit/gc_test.c` verifies object-list ordering, counter updates, and
   that compile-time arena allocation does not affect GC bookkeeping.

### Subtask 14.2 - Root traversal for stack values, call frames, and open upvalues

**Status:** `DONE`

**Depends on:** Subtask 14.1.

**Deliverables**

1. Mark objects reachable from the value stack.
2. Mark closures reachable from active call frames.
3. Mark objects held by open upvalues so captured locals survive until the
   closure no longer needs them.
4. Keep the traversal order deterministic so tests can exercise it reliably.

**Tests to add or update**

1. Extend `tests/unit/vm_core_test.c` with live stack-value coverage across a
   forced GC.
2. Extend `tests/unit/functions_closures_test.c` with a closure-capture
   regression that forces collection while a captured local is still open.
3. Reuse existing runtime coverage only where needed to exercise stack, frame,
   and upvalue survival together.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "runtime_core|closures|gc\.unit"
```

**Done when**

1. Values on the stack survive collection.
2. Frame-reachable closures survive collection.
3. Open upvalues keep captured values alive until the closure is closed.

**Implementation summary**

1. `ms_vm_gc_mark_roots()` now walks stack values, the current module, active
   call frames, and the open-upvalue list.
2. Recursive GC marking covers functions, closures, upvalues, receivers, chunk
   constants, module references, and the other runtime object relationships
   needed by the current root set.
3. Runtime and closure unit coverage now verifies stack, frame, closure,
   open-upvalue, and runtime-error cleanup behavior.

### Subtask 14.3 - Root traversal for modules, interned strings, and builtin/native registries

**Status:** `DONE`

**Depends on:** Subtasks 14.2 and Task 13.

**Deliverables**

1. Mark the current module and any frame-associated module references.
2. Mark the module cache so imported modules stay alive while still reachable
   from runtime state.
3. Mark interned strings so identifier storage stays stable.
4. Mark builtin/native registries so host-provided functions and classes remain
   callable after collection.

**Tests to add or update**

1. Extend `tests/unit/modules_test.c` and `tests/unit/string_test.c` for
   module-cache and interned-string survival.
2. Extend `tests/e2e/modules/cache_once.ms` and
   `tests/e2e/modules/shared_dependency_once.ms` so they force a GC cycle
   before reused imports.
3. Add a registry survival assertion in `tests/unit/vm_core_test.c` for
   builtin/native entries that are accessed after a collection cycle.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "gc\.unit|modules\.unit|runtime_core\.string|runtime_core\.vm_core"
```

**Done when**

1. Imported modules remain reachable through the runtime cache and module
   references.
2. Interned strings survive collection.
3. Builtin/native registry entries survive collection.

**Implementation summary**

1. `ms_vm_gc_mark_roots()` now walks the module cache in addition to the
   current module and active frames.
2. `ms_vm_gc_collect()` now performs a minimal mark-sweep pass so the tests can
   verify survival across a real collection cycle.
3. CLI test runs expose a private `__gc_collect__()` native so the module e2e
   cache cases can force collection between imports.
4. Module globals keep their string keys and runtime values reachable, so
   cached module state and native registrations stay alive through GC roots.
5. Unit coverage now exercises cached module globals, interned string survival,
   and native registry entries after collection.

### Subtask 14.4 - Temporary roots for compile-to-runtime and module-loading transitions

**Status:** `DONE`

**Depends on:** Subtasks 14.2 and 14.3.

**Deliverables**

1. Add a temporary-root mechanism for objects that are created during
   compilation, import resolution, or module loading but are not yet anchored in
   a permanent runtime root.
2. Make temporary roots explicit and short-lived so they cannot mask leaks in
   the permanent root set.
3. Ensure temporary-root cleanup happens even when module loading or execution
   fails.

**Tests to add or update**

1. Extend `tests/unit/gc_test.c` with a temporary-root lifetime test that
   allocates an object, anchors it temporarily, forces GC, and verifies that it
   survives until the temporary root is released.
2. Add an import or module-loading regression under `tests/e2e/modules/` that
   exercises temporary rooting across a successful load and a failing load.
3. Extend compile-to-runtime transition helpers only if they need to expose the
   temporary-root lifecycle directly.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "gc\.unit|modules\.|runtime_core"
```

**Done when**

1. Temporary roots keep transitional objects alive only for the duration of the
   transition.
2. Temporary roots do not leak into the permanent root set.
3. Cleanup runs on both success and failure paths.

**Implementation summary**

1. `MsVM` now carries an explicit temporary-root list and count alongside the
   collector-owned object list.
2. `ms_vm_gc_push_temporary_root()` and `ms_vm_gc_pop_temporary_root()` make
   transitional GC roots explicit, and `ms_vm_gc_mark_roots()` now marks them
   during collection.
3. `ms_vm_load_module()` roots the in-progress module object only after the
   temp-root registration succeeds, keeps it rooted until module initialization
   completes or fails, and GC cleanup removes any remaining temp root if the
   object is reclaimed.
4. `tests/unit/gc_test.c` now covers temporary-root lifetime directly, and the
   new `tests/e2e/modules/temp_root_success.ms` and
   `tests/e2e/modules/temp_root_failure.ms` regressions exercise the success
   and failure module-loading paths.
5. `ctest --test-dir build -C Debug --output-on-failure -R "gc\.unit|modules\.(cache_once|shared_dependency_once|import_runtime_failure|temp_root_success|temp_root_failure)|runtime_core"` passed after the change.

### Subtask 14.5 - Sweep/reclaim integration and unreachable-object tests

**Status:** `DONE`

**Depends on:** Subtasks 14.2, 14.3, and 14.4.

**Deliverables**

1. Add the sweep phase that reclaims unreachable GC-managed objects.
2. Update allocator and runtime ownership paths so freed objects are not
   reused unsafely after collection.
3. Keep collection behavior non-moving so surviving pointers remain valid.
4. Re-run GC at controlled points that make object-liveness tests deterministic.

**Tests to add or update**

1. Extend `tests/unit/gc_test.c` with an unreachable-object test that allocates
   a dead object graph, forces GC, and verifies that the objects are reclaimed.
2. Add a regression test that checks a live object and a dead object in the
   same heap cycle to confirm only the live object survives.
3. Extend the counter assertions in `tests/unit/gc_test.c` so tests can confirm
   that free counts advance when unreachable objects are reclaimed.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "gc\.unit|runtime_core"
```

**Done when**

1. Unreachable objects are reclaimed.
2. Reachable objects remain valid after repeated collections.
3. GC frees are observable through the counters exposed in
   `tests/unit/gc_test.c`.

**Implementation summary**

1. `tests/unit/gc_test.c` now covers a fully unreachable list graph, a mixed
   live/dead heap cycle, and a rooted module with class/native registrations,
   verifying that `ms_vm_gc_collect()` reclaims unreachable objects while
   preserving rooted ones.
2. The new assertions check `gc.free_count`, `gc.collection_count`, and the
   tracked-object list after each collection so sweep behavior stays
   deterministic.
3. `cmake --build build --config Debug` and
   `ctest --test-dir build -C Debug --output-on-failure -R "gc\\.unit|runtime_core"`
   both passed after the change.

### Subtask 14.6 - GC stress coverage, observability checks, and full regression gate

**Status:** `DONE`

**Depends on:** Subtask 14.5.

**Deliverables**

1. Add GC stress scripts that allocate heavily under repeated collections.
2. Keep any stress mode deterministic so it can run reliably in CI and local
   test runs.
3. Re-run the existing runtime and module suites with GC enabled, not just the
   isolated GC tests.
4. Lock the GC observability contract in tests so allocation, free, and
   collection counts stay meaningful.

**Tests to add or update**

1. Add `.ms` churn scripts under `tests/stress/gc/`.
2. Add a deterministic GC stress harness entry for GC-heavy workloads.
3. Include a regression that runs the module suite with GC enabled so the
   collector is exercised through real import and closure paths.

**Verification**

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "gc|stress|modules\."
```

**Done when**

1. GC stress tests complete without use-after-free behavior or obvious leak
   growth in fixed workloads.
2. The normal runtime and module suites still pass with GC enabled.
3. Collection metrics remain stable enough for tests to assert on them.

**Implementation summary**

1. `tests/stress/gc/alloc_churn.ms` and `tests/stress/gc/modules/module_gc.ms`
   now provide deterministic allocation churn and module-import coverage under
   repeated stress passes.
2. `tests/CMakeLists.txt` wires `gc.stress` through `tests/run_gc_stress.ps1`
   and `gc.modules` through `tests/run_gc_modules.ps1`, so the stress subset
   and the existing runtime/module suite both run under the GC-enabled runner.
3. `ctest --test-dir build -C Debug --output-on-failure -R "gc\\.modules|gc\\.stress|gc\\.unit|runtime_core|modules\\."`
   passed after the change.

## Acceptance

1. All subtasks above are complete in order.
2. Reachable objects survive and unreachable objects are reclaimed.
3. Module, string, and native registries stay alive through GC cycles.
4. Temporary roots protect transitional objects without leaking permanence.
5. Stress tests do not show use-after-free behavior or monotonic leaks in a
   fixed workload.
6. The task is not complete until build passes, tests pass, stress scripts run
   end to end, and all edited files are UTF-8 with LF and no trailing
   whitespace.

## Acceptance Commands

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "gc|stress|modules\."
```

## Out of Scope

1. Moving or generational GC.
2. Runtime performance tuning beyond correctness instrumentation.
