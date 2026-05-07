# Task 08 - Direct MSC Execution

## Status

Planned.

## Goal

Support direct execution of `.msc` instruction cache files as a hard-failure
mode that does not require the original source file.

## Design Links

- Direct `.msc` behavior and diagnostics:
  [msc-cache-design.md](msc-cache-design.md)

## Dependencies

1. Task 07 - Import Cache Integration.
2. Existing VM entry frame execution path.

## Scope

1. Detect `.msc` input in the CLI and route it to direct cache-file loading.
2. Read and validate the `.msc` header and payload without source metadata
   validation.
3. Execute the reconstructed chunk through the existing VM entry frame path.
4. Report diagnostics against the stored source display path when present,
   otherwise against the `.msc` path.
5. Treat invalid, incompatible, unreadable, or corrupt `.msc` files as hard
   failures.
6. Add tests showing direct `.msc` execution still works after the original
   entry `.ms` file is removed.

## Implementation Boundaries

1. `--no-cache` does not disable direct `.msc` execution.
2. Direct `.msc` execution must not require the original `.ms` source file.
3. Do not implement direct `.msc` dependency preference in this task.
4. Do not silently fall back to source for direct `.msc` input.
5. Keep normal runtime diagnostics for errors raised by executing valid cached
   chunks.

## File Ownership

1. Modify `src/main.c`.
2. Modify `include/ms/cache/cache_file.h` and `src/cache/cache_file.c` only if
   direct-load mode needs a separate validation option.
3. Add end-to-end tests under `tests/e2e/mscache/`.
4. Modify `tests/CMakeLists.txt` for direct `.msc` tests.

## TDD Plan

1. Add a failing test that runs a source file once to create `.msc`, deletes or
   moves the source file, then executes the `.msc` successfully.
2. Add a failing test where corrupt direct `.msc` input fails with a clear
   error.
3. Add a failing test where incompatible format or ABI versions fail.
4. Add a failing test proving `--no-cache path/to/file.msc` still executes the
   `.msc` file or rejects the invalid option consistently with the chosen CLI
   parsing rules.
5. Implement direct-load CLI routing and diagnostics.

## Acceptance

1. `mslangc __mscache__/script.msc` executes a valid cache file.
2. The original `.ms` entry file is not required for direct `.msc` execution.
3. Corrupt or incompatible direct `.msc` files fail hard and clearly.
4. Direct `.msc` execution reports runtime diagnostics against the stored source
   display path when available.
5. Source `.ms` cache fallback behavior from earlier tasks remains unchanged.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "mscache"
```

## Out of Scope

1. Dependency `.msc` preference during direct `.msc` execution.
2. Packaging cache files.
3. Cache file signing or trust policy.
4. Hash invalidation.
