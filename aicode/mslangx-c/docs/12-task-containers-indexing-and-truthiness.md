# Task 12 - Containers, Indexing, and Truthiness

## Status

In progress. Subtasks 12.1 through 12.3 completed and verified on 2026-04-03.

## Goal

Implement containers as first-class values and finish the full truthiness model required by the language design.

## Design Links

- Container values, truthiness, and VM container opcodes: [../mslangc-design.md](../mslangc-design.md)
- Repository rules: [../AGENTS.md](../AGENTS.md)

## Dependencies

1. Task 03
2. Task 06
3. Task 08
4. Task 10

## Scope

1. Implement `list`, `tuple`, and `map` runtime objects.
2. Lower container literals and emit `BUILD_*` instructions.
3. Implement index read and write operations.
4. Complete `ms_value_is_falsey()` and implement `ms_value_length()`.
5. Support container printing and runtime error reporting.

## Implementation Boundaries

1. This task owns runtime and lowering behavior for containers.
2. Parser support for literal syntax is assumed from Tasks 05 and 06.
3. `map` in v1 supports string keys only.
4. String indexing is out of scope in v1 and must fail at runtime if routed to
   generic indexing support.
5. Tuple mutation is a runtime error with `MS4xxx`, not a static error.

## File Ownership

1. Container object definitions and helpers in `include/ms/object.h`,
   `include/ms/value.h`, `include/ms/runtime/opcode.h`,
   `include/ms/runtime/vm.h`, `src/runtime/object.c`, `src/runtime/value.c`,
   `src/runtime/disasm.c`, and `src/runtime/vm.c`.
2. Container lowering in `src/frontend/lowering_basic.c`.
3. Test registration in `tests/CMakeLists.txt` and `tests/unit/CMakeLists.txt`.
4. Container unit tests in `tests/unit/containers_truthiness_test.c` and
   value-helper extensions in `tests/unit/value_test.c` when needed.
5. End-to-end scripts under `tests/e2e/containers/`.

## Diagnostics Contract

1. Invalid index target, non-integral indices, out-of-range access, missing map
   keys, and tuple writes are `phase=runtime` with `MS4xxx`.
2. Non-string map keys in v1 are `phase=runtime` with `MS4xxx`.
3. Truthiness behavior must be observable through tests, not only helper-level
   unit assertions.

## Execution Breakdown

Every subtask must start with failing tests, then the minimal implementation,
then the focused verification command listed for that subtask.

## Subtask Progress

Use the following status markers consistently in this document:

- `TODO`: not started
- `DOING`: in progress
- `BLOCKED`: waiting on another subtask or prerequisite fix
- `DONE`: completed and verified

| Subtask | Status | Depends on | Summary |
| --- | --- | --- | --- |
| 12.1 | `DONE` | Task 03 | Runtime container objects, storage helpers, and printing hooks |
| 12.2 | `DONE` | 12.1 | Truthiness and `ms_value_length()` helpers |
| 12.3 | `DONE` | 12.1 | Lower container literals and execute `BUILD_*` opcodes |
| 12.4 | `TODO` | 12.3 | `list` and `tuple` index read and write semantics |
| 12.5 | `TODO` | 12.4 | `map` indexing with string keys |
| 12.6 | `TODO` | 12.5 | Negative runtime diagnostics and full regression |

### Subtask 12.1 - Runtime container objects, storage helpers, and printing hooks

**Status:** `DONE`

**Depends on:** Task 03 runtime value and object foundation.

**Deliverables**

1. Define `list`, `tuple`, and `map` runtime storage on top of the existing
   `MsObject` header.
2. Add constructors, destructors, and helpers for sequence length, element
   storage, and map table ownership.
3. Keep object-type names, printing hooks, and value predicates consistent with
   the shared runtime object model.

**Tests to add or update**

1. Add `tests/unit/containers_truthiness_test.c` for container allocation,
   empty-state invariants, and direct storage inspection.
2. Register a unit target named `containers_truthiness.unit` in
   `tests/unit/CMakeLists.txt`.
3. Extend `tests/unit/value_test.c` only if value-level predicates or printing
   helpers need explicit assertions.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "containers_truthiness\.unit|runtime_core\.(value|string|table)"
```

**Done when**

1. The runtime can allocate and free all three container object kinds without
   leaks or type mismatches.
2. Unit tests can inspect empty and populated container storage directly.

### Subtask 12.2 - Truthiness and `ms_value_length()` helpers

**Status:** `DONE`

**Depends on:** Subtask 12.1.

**Deliverables**

1. Complete `ms_value_is_falsey()` for `nil`, `false`, numeric zero, empty
   string, empty `list`, empty `tuple`, and empty `map`.
2. Implement `ms_value_length()` for `string`, `list`, `tuple`, and `map`,
   while reporting failure for unsupported value kinds.
3. Make sure runtime conditional execution observes the same truthiness helper
   path used by unit tests.

**Tests to add or update**

1. Extend `tests/unit/containers_truthiness_test.c` with helper-level
   truthiness and length coverage.
2. Add `tests/e2e/containers/truthiness.ms` and
   `tests/e2e/containers/truthiness.ms.out`.
3. Add `tests/e2e/containers/length.ms` and
   `tests/e2e/containers/length.ms.out`.
4. Register `containers.truthiness` and `containers.length` in
   `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "containers_truthiness\.unit|containers\.(truthiness|length)"
```

**Done when**

1. Empty string and empty containers are false during actual script execution.
2. `ms_value_length()` returns stable lengths for `string`, `list`, `tuple`,
   and `map`.

### Subtask 12.3 - Lower container literals and execute `BUILD_*` opcodes

**Status:** `DONE`

**Depends on:** Subtask 12.1.

**Deliverables**

1. Lower `list`, `tuple`, and `map` literals into `BUILD_LIST`,
   `BUILD_TUPLE`, and `BUILD_MAP`.
2. Preserve element and entry evaluation order when lowering literals.
3. Execute the new container-build opcodes in the VM and keep disassembly
   output readable.

**Tests to add or update**

1. Extend `tests/unit/lowering_basic_test.c` with `BUILD_*` opcode coverage.
2. Add `tests/e2e/containers/literals.ms` and
   `tests/e2e/containers/literals.ms.out`.
3. Register `containers.literals` in `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "lowering_basic\.unit|containers\.literals"
```

**Done when**

1. Container literals compile without fallback or unsupported-feature
   diagnostics.
2. Scripts can construct and print `list`, `tuple`, and `map` values end to
   end.

### Subtask 12.4 - `list` and `tuple` index read and write semantics

**Status:** `TODO`

**Depends on:** Subtask 12.3.

**Deliverables**

1. Implement `INDEX_GET` for `list` and `tuple` with numeric, integral,
   in-range indices only.
2. Implement `INDEX_SET` for `list` writes.
3. Route tuple writes to runtime diagnostics instead of mutating tuple storage.

**Tests to add or update**

1. Add `tests/e2e/containers/list_indexing.ms` and
   `tests/e2e/containers/list_indexing.ms.out`.
2. Add `tests/e2e/containers/tuple_index_read.ms` and
   `tests/e2e/containers/tuple_index_read.ms.out`.
3. Extend `tests/unit/containers_truthiness_test.c` with direct sequence access
   assertions if indexing helpers are exposed.
4. Register `containers.list_indexing` and `containers.tuple_index_read` in
   `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "containers_truthiness\.unit|containers\.(list_indexing|tuple_index_read)"
```

**Done when**

1. `list` supports indexed reads and writes with visible later reads.
2. `tuple` supports indexed reads while remaining immutable.

### Subtask 12.5 - `map` indexing with string keys

**Status:** `TODO`

**Depends on:** Subtask 12.4.

**Deliverables**

1. Implement `INDEX_GET` and `INDEX_SET` for `map`.
2. Require string keys on all `map` indexing paths in v1.
3. Support insert-or-replace writes and later reads through the same key.

**Tests to add or update**

1. Add `tests/e2e/containers/map_indexing.ms` and
   `tests/e2e/containers/map_indexing.ms.out`.
2. Extend `tests/unit/containers_truthiness_test.c` with direct `map`
   read-write assertions if helpers are exposed.
3. Register `containers.map_indexing` in `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "containers_truthiness\.unit|containers\.map_indexing"
```

**Done when**

1. `map` supports string-key reads and writes in end-to-end scripts.
2. Later reads observe replaced values for existing keys.

### Subtask 12.6 - Negative runtime diagnostics and full regression

**Status:** `TODO`

**Depends on:** Subtask 12.5.

**Deliverables**

1. Cover invalid index targets, non-integral indices, out-of-range access,
   tuple writes, missing map keys, string indexing, and non-string map keys
   with stable runtime diagnostics.
2. Make sure all new container tests are wired into CMake and script runners.
3. Run the full container-focused acceptance sweep and fix remaining
   integration gaps before marking Task 12 complete.

**Tests to add or update**

1. Add `tests/e2e/containers/invalid_index_target.ms`.
2. Add `tests/e2e/containers/non_integral_index.ms`.
3. Add `tests/e2e/containers/index_out_of_range.ms`.
4. Add `tests/e2e/containers/tuple_write.ms`.
5. Add `tests/e2e/containers/missing_map_key.ms`.
6. Add `tests/e2e/containers/string_indexing.ms`.
7. Add `tests/e2e/containers/non_string_map_key.ms`.
8. Register the negative container CLI tests in `tests/CMakeLists.txt` with
   `EXPECT_EXIT=70`.
9. Keep at least one passing script that exercises the chain
   `literal -> truthiness -> list write -> tuple read -> map read`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "containers|truthiness|lowering_basic\.unit"
build\Debug\mslangc.exe tests\e2e\containers\map_indexing.ms
```

**Done when**

1. Runtime failures report `phase=runtime` and stable `MS4xxx` codes.
2. All Task 12 tests pass from CTest and at least one representative `.ms`
   script runs directly through the CLI.

## Acceptance

1. All subtasks above are complete in order.
2. Empty string and empty containers are false in conditional execution.
3. `list` supports indexed reads and writes.
4. `tuple` is readable but not writable.
5. `map` supports string-key reads and writes in v1.
6. `ms_value_length()` works for `string`, `list`, `tuple`, and `map`.
7. The task is not complete until build passes, tests pass, `.ms` scripts run
   end to end, and all edited files are UTF-8 with LF and no trailing
   whitespace.

## Acceptance Commands

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "containers|truthiness|lowering_basic\.unit"
build\Debug\mslangc.exe tests\e2e\containers\map_indexing.ms
```

## Out of Scope

1. Module cache behavior.
2. GC optimization.
