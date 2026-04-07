# Task 13 - Modules and Import System

## Status

In progress. Subtasks 13.1 and 13.2 are `DONE`; subtasks 13.3 through 13.8 are `TODO`.

## Goal

Implement module loading, caching, and import binding so multi-file programs
can run with stable module diagnostics.

## Design Links

- Module names, state machine, import behavior, and module diagnostics:
  [../mslangc-design.md](../mslangc-design.md)
- Repository rules: [../AGENTS.md](../AGENTS.md)

## Dependencies

1. Task 06
2. Task 08
3. Task 10

## Scope

1. Implement module objects, per-module namespaces, and module cache state.
2. Map dotted module names to file paths.
3. Support `import` and `from ... import ... as ...` end to end.
4. Support default export of top-level `var`, `fn`, and `class` bindings.
5. Bind imported modules and symbols according to the design baseline.
6. Emit module diagnostics `MS5001` through `MS5004`.

## Implementation Boundaries

1. This task owns import resolution, cache management, and export binding.
2. It does not introduce explicit `export` syntax.
3. The module state machine must be explicit:
   `UNSEEN -> INITIALIZING -> INITIALIZED`, and any failure moves the module to
   `FAILED`.
4. Re-entry into a module already in `INITIALIZING` is a cycle and must report
   `MS5003`.
5. Cache keys must use the canonical resolved file path, not only the raw
   import string.
6. `import a.b` binds the module object to `b` unless `as` overrides the local
   name.
7. `from ... import ...` binds a snapshot value after successful module
   initialization; imports are not live bindings in v1.
8. Support for importing classes or containers can be added as later
   integration coverage once those features land, but module core must not wait
   on them.

## File Ownership

1. Runtime module loader, cache, and import opcodes in
   `include/ms/runtime/vm.h`, `include/ms/runtime/opcode.h`,
   `src/runtime/vm.c`, and `src/runtime/disasm.c`.
2. Import lowering and binding support in `src/frontend/lowering_basic.c`,
   `src/frontend/resolver.c`, and `src/frontend/resolution_table.c` when
   additional metadata is needed.
3. CLI module search-path wiring in `src/main.c`.
4. Test registration in `tests/CMakeLists.txt` and `tests/unit/CMakeLists.txt`.
5. Module unit tests in `tests/unit/modules_test.c`,
   `tests/unit/lowering_basic_test.c`, `tests/unit/resolver_test.c`, and
   `tests/unit/vm_core_test.c` when helper coverage is needed.
6. Resolver-only module scripts under `tests/ms/resolver/`.
7. Multi-file fixtures and end-to-end scripts under `tests/fixtures/modules/`
   and `tests/e2e/modules/`.

## Diagnostics Contract

1. `MS5001`: module file not found.
2. `MS5002`: exported symbol not found.
3. `MS5003`: cyclic dependency.
4. `MS5004`: module initialization failed because parse, resolve, or runtime
   execution failed inside the imported module.
5. Imported-module failures should preserve the originating diagnostic and may
   add an import-site `MS5004` wrapper.
6. Module diagnostic tests must lock `phase + code + line` through exact stderr
   fragments in CMake registrations, even before Task 15 centralizes broader
   golden coverage.

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
| 13.1 | `DONE` | Task 03, Task 08 | Module loader state, search roots, and canonical cache keys |
| 13.2 | `DONE` | 13.1 | Lower import statements and bind local names |
| 13.3 | `TODO` | 13.2 | End-to-end `import a` and `import a.b as alias` |
| 13.4 | `TODO` | 13.3 | End-to-end `from a import b as c` and snapshot bindings |
| 13.5 | `TODO` | 13.4 | Cache reuse and namespace isolation |
| 13.6 | `TODO` | 13.3 | `MS5001` missing-module diagnostics |
| 13.7 | `TODO` | 13.4 | `MS5002` missing-export diagnostics |
| 13.8 | `TODO` | 13.5, 13.6, 13.7 | `MS5003` and `MS5004` failures plus full module regression |

### Subtask 13.1 - Module loader state, search roots, and canonical cache keys

**Status:** `DONE`

**Depends on:** Task 03 runtime object and table support, plus Task 08 CLI
runner wiring.

**Deliverables**

1. Extend `MsModule` and `MsVM` with explicit module state, canonical file
   identifier storage, search-root configuration, and a module cache keyed by
   canonical resolved path.
2. Add helpers that map dotted names such as `a.b` to `a/b.ms`, search roots
   in order, and normalize the resolved file path before cache lookup.
3. Wire the CLI so the entry script can seed module search roots
   deterministically.
4. Keep state transitions explicit in runtime helpers before imported code
   execution is added.

**Tests to add or update**

1. Add `tests/unit/modules_test.c` for dotted-path mapping, search-root
   precedence, canonical cache-key normalization, and module-state transitions.
2. Register a unit target named `modules.unit` in `tests/unit/CMakeLists.txt`.
3. Extend `tests/unit/vm_core_test.c` only if VM search-root setup helpers need
   direct coverage.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "modules\.unit|runtime_core\.vm_core"
```

**Done when**

1. Unit tests can create and update module records through `UNSEEN`,
   `INITIALIZING`, `INITIALIZED`, and `FAILED`.
2. Dotted import names resolve deterministically to canonical `.ms` file paths.

### Subtask 13.2 - Lower import statements and bind local names

**Status:** `DONE`

**Depends on:** Subtask 13.1.

**Deliverables**

1. Add `MS_OP_IMPORT_MODULE` and `MS_OP_IMPORT_SYMBOL` to the opcode set and
   disassembler output.
2. Lower `import path [as alias]` and
   `from path import name [as alias]` into those opcodes without changing the
   existing parser AST shape.
3. Bind the imported leaf name or explicit alias into the current scope and
   reject duplicate binding collisions through the resolver.

**Tests to add or update**

1. Extend `tests/unit/lowering_basic_test.c` with opcode coverage for both
   import forms.
2. Extend `tests/unit/resolver_test.c` with positive import-binding coverage.
3. Keep `tests/ms/resolver/duplicate_module_binding.ms` and add
   `tests/ms/resolver/duplicate_import_alias_binding.ms` if alias collisions
   need a dedicated resolver fixture.
4. Register `resolver_static.duplicate_import_alias_binding` in
   `tests/CMakeLists.txt` if the new fixture is added.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "lowering_basic\.unit|resolver\.unit|resolver_static\.(duplicate_module_binding|duplicate_import_alias_binding)"
```

**Done when**

1. Lowering emits dedicated import opcodes in source order.
2. Resolver binds the import leaf or alias exactly once in the current scope.

### Subtask 13.3 - End-to-end `import a` and `import a.b as alias`

**Status:** `TODO`

**Depends on:** Subtask 13.2.

**Deliverables**

1. Execute `MS_OP_IMPORT_MODULE` by loading, compiling, and running the target
   module exactly once on its first successful import.
2. Expose the imported module object to the caller under the leaf name or
   explicit alias.
3. Export top-level `var`, `fn`, and `class` bindings by default through the
   imported module namespace.
4. Restore the caller module context after the imported module finishes
   initialization.

**Tests to add or update**

1. Add `tests/fixtures/modules/basic/math.ms`.
2. Add `tests/fixtures/modules/pkg/tool.ms`.
3. Add `tests/e2e/modules/import_basic.ms` and
   `tests/e2e/modules/import_basic.ms.out`.
4. Add `tests/e2e/modules/import_alias.ms` and
   `tests/e2e/modules/import_alias.ms.out`.
5. Register `modules.import_basic` and `modules.import_alias` in
   `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "lowering_basic\.unit|modules\.(import_basic|import_alias)"
```

**Done when**

1. `import basic.math` binds the module object to `math`.
2. `import pkg.tool as alias` binds the same module object to `alias`.
3. Importing a module does not merge its top-level names into the caller
   implicitly.

### Subtask 13.4 - End-to-end `from a import b as c` and snapshot bindings

**Status:** `TODO`

**Depends on:** Subtask 13.3.

**Deliverables**

1. Execute `MS_OP_IMPORT_SYMBOL` only after the target module reaches
   `INITIALIZED`.
2. Resolve the requested exported symbol from the imported module namespace and
   bind it to the requested local name or alias.
3. Keep import semantics as snapshot binding in v1: later reassignment inside
   the exporting module must not update the already-imported local binding.

**Tests to add or update**

1. Add `tests/fixtures/modules/basic/exports.ms`.
2. Add `tests/e2e/modules/from_import.ms` and
   `tests/e2e/modules/from_import.ms.out`.
3. Add `tests/e2e/modules/from_import_alias.ms` and
   `tests/e2e/modules/from_import_alias.ms.out`.
4. Add `tests/e2e/modules/from_import_snapshot.ms` and
   `tests/e2e/modules/from_import_snapshot.ms.out`.
5. Register `modules.from_import`, `modules.from_import_alias`, and
   `modules.from_import_snapshot` in `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "modules\.(from_import|from_import_alias|from_import_snapshot)"
```

**Done when**

1. `from basic.exports import value` binds the exported value into local scope.
2. `from basic.exports import value as alias` binds the same exported value to
   `alias`.
3. Imported names stay as snapshots instead of live bindings.

### Subtask 13.5 - Cache reuse and namespace isolation

**Status:** `TODO`

**Depends on:** Subtask 13.4.

**Deliverables**

1. Reuse cached `INITIALIZED` modules on repeated imports instead of re-running
   top-level code.
2. Keep partially loaded modules out of successful caller-visible paths until
   initialization finishes.
3. Prove module namespaces stay isolated except through explicit `import` and
   `from ... import ...`.

**Tests to add or update**

1. Add `tests/fixtures/modules/cache/counter.ms`.
2. Add `tests/e2e/modules/cache_once.ms` and
   `tests/e2e/modules/cache_once.ms.out`.
3. Add `tests/e2e/modules/shared_dependency_once.ms` and
   `tests/e2e/modules/shared_dependency_once.ms.out`.
4. Add `tests/e2e/modules/namespace_isolation.ms` and
   `tests/e2e/modules/namespace_isolation.ms.out`.
5. Register `modules.cache_once`, `modules.shared_dependency_once`, and
   `modules.namespace_isolation` in `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "modules\.(cache_once|shared_dependency_once|namespace_isolation)"
```

**Done when**

1. Repeated imports observe only one top-level side-effect run per module.
2. Shared dependencies are initialized once even when imported through multiple
   parents.
3. Module globals remain isolated unless explicitly imported.

### Subtask 13.6 - `MS5001` missing-module diagnostics

**Status:** `TODO`

**Depends on:** Subtask 13.3.

**Deliverables**

1. Report `MS5001` when no configured search root resolves the requested module
   file.
2. Anchor the diagnostic at the importing statement line in the caller module.
3. Leave the failed cache entry in `FAILED` state so repeated lookups stay
   deterministic within the same run.

**Tests to add or update**

1. Add `tests/e2e/modules/missing_module.ms`.
2. Register `modules.missing_module` in `tests/CMakeLists.txt` with
   `EXPECT_EXIT=70`.
3. Lock stderr fragments for `phase=module`, `code=MS5001`, and the exact
   `missing_module.ms:<line>:` import-site location.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "modules\.missing_module"
```

**Done when**

1. Missing module loads fail with `phase=module` and `code=MS5001`.
2. The failure points at the importing source line rather than a generic loader
   location.

### Subtask 13.7 - `MS5002` missing-export diagnostics

**Status:** `TODO`

**Depends on:** Subtask 13.4.

**Deliverables**

1. Report `MS5002` when a `from ... import ...` name is absent from the
   imported module namespace after successful initialization.
2. Anchor the diagnostic at the importing statement line.
3. Preserve normal `import module` behavior even when a later
   `from ... import ...` statement fails in the same script.

**Tests to add or update**

1. Add `tests/fixtures/modules/basic/partial_exports.ms`.
2. Add `tests/e2e/modules/missing_export.ms`.
3. Register `modules.missing_export` in `tests/CMakeLists.txt` with
   `EXPECT_EXIT=70`.
4. Lock stderr fragments for `phase=module`, `code=MS5002`, and the exact
   `missing_export.ms:<line>:` import-site location.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "modules\.(missing_export|from_import_snapshot)"
```

**Done when**

1. Missing exported names fail with `MS5002` after module initialization
   succeeds.
2. Snapshot import behavior from Subtask 13.4 still passes alongside the
   failure case.

### Subtask 13.8 - `MS5003` and `MS5004` failures plus full module regression

**Status:** `TODO`

**Depends on:** Subtasks 13.5, 13.6, and 13.7.

**Deliverables**

1. Detect `INITIALIZING` re-entry as `MS5003` and move the participating module
   records to `FAILED`.
2. Wrap parse, resolve, or runtime failures from imported modules as `MS5004`
   while preserving the originating diagnostic that caused the failure.
3. Run the full module-focused regression sweep and keep every `MS500x` case
   line-locked through stderr fragment assertions in CMake.

**Tests to add or update**

1. Add `tests/fixtures/modules/cycle/a.ms`.
2. Add `tests/fixtures/modules/cycle/b.ms`.
3. Add `tests/e2e/modules/cyclic_dependency.ms`.
4. Add `tests/fixtures/modules/failures/parse_fail.ms`.
5. Add `tests/fixtures/modules/failures/resolve_fail.ms`.
6. Add `tests/fixtures/modules/failures/runtime_fail.ms`.
7. Add `tests/e2e/modules/import_parse_failure.ms`.
8. Add `tests/e2e/modules/import_resolve_failure.ms`.
9. Add `tests/e2e/modules/import_runtime_failure.ms`.
10. Add `tests/e2e/modules/regression_chain.ms` and
    `tests/e2e/modules/regression_chain.ms.out`.
11. Register negative tests in `tests/CMakeLists.txt` with `EXPECT_EXIT=70`
    and line-locked stderr fragments for `MS5003` and `MS5004`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "modules\.|modules\.unit|lowering_basic\.unit|resolver\.unit"
build\Debug\mslangc.exe tests\e2e\modules\regression_chain.ms
```

**Done when**

1. Cycles fail with `phase=module` and `code=MS5003`.
2. Imported parse, resolve, and runtime failures preserve the original
   diagnostic and add import-site `MS5004` context when appropriate.
3. All module tests pass from CTest and at least one representative multi-file
   script runs directly through the CLI.

## Acceptance

1. All subtasks above are complete in order.
2. `import` and `from ... import ...` execute correctly across multiple files.
3. Module cache behavior is deterministic and avoids duplicate initialization.
4. Module namespaces stay isolated except through explicit imports.
5. All module error codes are locked by tests to at least `phase + code + line`.
6. The task is not complete until build passes, tests pass, `.ms` fixtures run
   end to end, and all edited files are UTF-8 with LF and no trailing
   whitespace.

## Acceptance Commands

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "modules\.|modules\.unit|lowering_basic\.unit|resolver\.unit"
build\Debug\mslangc.exe tests\e2e\modules\regression_chain.ms
```

## Out of Scope

1. Explicit `export` syntax.
2. GC collection policy details.
