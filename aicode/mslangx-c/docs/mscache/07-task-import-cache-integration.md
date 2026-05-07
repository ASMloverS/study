# Task 07 - Import Cache Integration

## Status

Planned.

## Goal

Use the shared load service for imported modules while preserving the existing
module state machine, diagnostics, and namespace behavior.

## Design Links

- Import cache behavior:
  [msc-cache-design.md](msc-cache-design.md)
- Existing module task:
  [../13-task-modules-and-import-system.md](../13-task-modules-and-import-system.md)

## Dependencies

1. Task 06 - CLI Cache Policy.
2. Existing module loader and `MsModuleState` transitions.

## Scope

1. Pass the entry script cache policy into the VM so imports use the same cache
   setting.
2. Replace imported module source compile paths with the shared source load
   service.
3. Generate one `.msc` file per imported `.ms` module.
4. Reuse valid imported-module cache files on later process runs.
5. Preserve cycle detection, failed-module handling, import-site diagnostics,
   and module namespace isolation.

## Implementation Boundaries

1. Disk cache must not bypass `UNSEEN -> INITIALIZING -> INITIALIZED` or
   failure transitions.
2. Disk cache must not bypass canonical module cache keys inside one VM process.
3. `--no-cache` disables imported-module cache reads and writes as well as the
   entry-script cache.
4. Do not add direct `.msc` imports for source-free entry execution in this
   task.
5. Do not change import language semantics.

## File Ownership

1. Modify `include/ms/runtime/vm.h` if VM options need cache policy fields.
2. Modify `src/runtime/vm.c`.
3. Modify `src/main.c` to pass cache policy into the VM.
4. Add or update fixtures under `tests/fixtures/modules/`.
5. Add end-to-end tests under `tests/e2e/mscache/`.
6. Modify `tests/CMakeLists.txt` for new module cache tests.

## TDD Plan

1. Add a failing test where importing a module creates
   `module_dir/__mscache__/module.msc`.
2. Add a failing test where a second process run reuses the module cache and
   preserves output.
3. Add a failing test where `--no-cache` prevents cache files for both entry
   script and imports.
4. Add failing regression tests for cyclic imports and module initialization
   failure with cache enabled.
5. Implement VM policy plumbing and module source-loader integration.

## Acceptance

1. Imported modules generate and reuse their own `.msc` files.
2. `--no-cache` disables cache reads and writes for imports.
3. Module state transitions remain explicit and covered by tests.
4. Existing module diagnostics `MS5001` through `MS5004` remain stable.
5. Existing module namespace isolation tests pass with cache enabled.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "mscache|modules"
```

## Out of Scope

1. Direct execution of `.msc` files.
2. Source-free import trees.
3. Cache file version migration.
4. Hash-based invalidation.
