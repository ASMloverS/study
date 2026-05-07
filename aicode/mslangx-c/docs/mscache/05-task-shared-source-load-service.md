# Task 05 - Shared Source Load Service

## Status

Planned.

## Goal

Add one shared load service that compiles `.ms` source or returns a valid cached
chunk for both CLI entry execution and imported modules.

## Design Links

- Shared loading architecture:
  [msc-cache-design.md](msc-cache-design.md)
- Module loading baseline:
  [../13-task-modules-and-import-system.md](../13-task-modules-and-import-system.md)

## Dependencies

1. Task 04 - Cache File I/O and Validation.
2. Existing `ms_compile_source()` API.
3. Existing source-file reading logic in `src/main.c` and
   `src/runtime/vm.c`.

## Scope

1. Introduce a load options object with cache enabled/disabled policy and entry
   kind.
2. Add a shared function that derives the cache path, stats the source, tries a
   valid cache load, otherwise reads and compiles source.
3. After successful source compilation, attempt an atomic cache write when cache
   policy allows it.
4. Return enough metadata for callers to preserve source display paths and
   distinguish cache-hit chunks from freshly compiled chunks for tests.
5. Add unit or integration tests using temporary source files that prove the
   service can compile, write, and later read a cache without touching CLI
   parsing.

## Implementation Boundaries

1. Cache read, cache write, and cache directory creation failures for `.ms`
   source must be soft failures.
2. The service must not bypass existing parse, resolve, lowering, or runtime
   diagnostics on source fallback.
3. The service must not own module state transitions; VM import integration
   happens in a later task.
4. Do not add direct `.msc` execution entry behavior here.
5. Inline `-e` source remains outside this service unless existing code already
   represents it as a file path.

## File Ownership

1. Create `include/ms/cache/source_loader.h`.
2. Create `src/cache/source_loader.c`.
3. Modify `CMakeLists.txt` or the existing source list to compile the new file.
4. Create `tests/unit/source_loader_test.c` or
   `tests/integration/source_loader_test.c`, following existing test patterns.
5. Modify the appropriate test `CMakeLists.txt` to register
   `source_loader.unit` or `source_loader.integration`.

## TDD Plan

1. Add a failing test where a source file compiles and creates a sibling
   `__mscache__/script.msc`.
2. Add a failing test where a second load of unchanged source returns a
   cache-hit result.
3. Add a failing test where corrupt cache data falls back to source and
   succeeds.
4. Add a failing test where cache disabled skips both read and write.
5. Implement the shared service until these tests pass without changing CLI
   behavior.

## Acceptance

1. The shared load-service tests pass.
2. Source fallback behavior remains reliable when cache files are missing,
   corrupt, stale, or unwritable.
3. Cache-disabled mode does not create `__mscache__/`.
4. The service exposes a clear API for both CLI and VM import callers.
5. Existing CLI and module tests continue to pass.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "source_loader|cache_file\.unit|chunk_codec\.unit"
```

## Out of Scope

1. User-visible `--no-cache` parsing.
2. Replacing CLI entry execution with the service.
3. Replacing VM import execution with the service.
4. Direct `.msc` execution.
