# Task 09 - Direct MSC Imports and Release Gate

## Status

Planned.

## Goal

Complete direct `.msc` import behavior and verify the full disk instruction
cache feature end to end.

## Design Links

- Direct `.msc` import preference and acceptance tests:
  [msc-cache-design.md](msc-cache-design.md)
- Release gate baseline:
  [../15-task-conformance-golden-diagnostics-and-release-gate.md](../15-task-conformance-golden-diagnostics-and-release-gate.md)

## Dependencies

1. Task 08 - Direct MSC Execution.
2. Existing module search-root and canonical path behavior.

## Scope

1. During direct `.msc` entry execution, resolve imports to normal logical
   `.ms` paths when possible.
2. Prefer matching dependency cache files under `__mscache__/module.msc` when
   they exist and are compatible.
3. Fall back to `.ms` source imports when dependency cache files are absent,
   stale, or invalid and source exists.
4. Support source-free direct `.msc` entry execution when dependency `.msc`
   files are present.
5. Add final regression coverage for cache creation, cache reuse, invalidation,
   corruption handling, `--no-cache`, imports, direct execution, and direct
   import fallback.
6. Run the repository acceptance gate.

## Implementation Boundaries

1. Direct `.msc` import preference must not change normal `.ms` import behavior.
2. Direct `.msc` dependency cache corruption may fall back to source only when
   the logical `.ms` source exists.
3. Source-free dependency loading requires compatible `.msc` files.
4. Do not introduce new module naming syntax or package metadata.
5. Do not add hash-based invalidation in this task.

## File Ownership

1. Modify `src/runtime/vm.c`.
2. Modify `include/ms/runtime/vm.h` if import load mode needs VM-level policy.
3. Modify `src/cache/source_loader.c` if direct-entry import preference belongs
   in the shared service.
4. Add end-to-end fixtures under `tests/e2e/mscache/`.
5. Add multi-file fixtures under `tests/fixtures/mscache/`.
6. Modify `tests/CMakeLists.txt` for the final cache regression suite.

## TDD Plan

1. Add a failing test where direct `.msc` entry execution imports a dependency
   from a compatible dependency `.msc` file after dependency source removal.
2. Add a failing test where direct `.msc` entry execution falls back to a
   dependency `.ms` source file when dependency `.msc` is absent.
3. Add a failing test where corrupt dependency `.msc` falls back to dependency
   source when source exists.
4. Add a failing test where corrupt dependency `.msc` fails when no dependency
   source exists.
5. Add or update a final `mscache` regression selector that covers every test
   listed in `msc-cache-design.md`.
6. Implement import preference and run the focused regression selector before
   the full gate.

## Acceptance

1. Direct `.msc` entry execution can import compatible dependency `.msc` files.
2. Direct `.msc` entry execution can fall back to dependency `.ms` source when
   available.
3. Source-free direct `.msc` execution works for an entry file and dependencies
   when all required `.msc` files exist.
4. All focused `mscache` tests pass.
5. The full build and test gate passes.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "mscache"
ctest --test-dir build -C Debug --output-on-failure
```

## Out of Scope

1. Cache packaging or install commands.
2. Hash-based invalidation.
3. Verbose cache tracing.
4. Cache optimization beyond the stable v1 file format.
