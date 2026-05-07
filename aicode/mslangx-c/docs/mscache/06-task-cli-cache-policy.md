# Task 06 - CLI Cache Policy

## Status

Planned.

## Goal

Wire the shared load service into CLI file execution so `.ms` scripts use disk
cache by default and `--no-cache` disables it.

## Design Links

- CLI interface and source execution behavior:
  [msc-cache-design.md](msc-cache-design.md)

## Dependencies

1. Task 05 - Shared Source Load Service.
2. Existing CLI option parsing and entry execution in `src/main.c`.

## Scope

1. Add `--no-cache` to the accepted CLI options.
2. Use the shared source load service for `mslangc script.ms`.
3. Keep `mslangc -e "code"` on the current direct compile-and-run path with no
   disk cache.
4. Prove first `.ms` execution creates `__mscache__/script.msc`.
5. Prove second `.ms` execution uses a valid cache and preserves program
   output.
6. Prove stale or corrupt automatic cache files fall back to source execution.

## Implementation Boundaries

1. `--no-cache` affects `.ms` entry scripts and must later be passed to imports.
2. Unknown options keep the existing usage-error behavior.
3. Do not support direct `.msc` execution in this task.
4. Do not print cache-hit or cache-miss diagnostics.
5. Keep cache failures soft for `.ms` execution.

## File Ownership

1. Modify `src/main.c`.
2. Modify CLI test registration in `tests/CMakeLists.txt`.
3. Add end-to-end fixtures under `tests/e2e/mscache/`.
4. Add CMake or script helpers only if existing test helpers cannot check file
   creation and repeated execution.

## TDD Plan

1. Add a failing CLI test for `mslangc --no-cache script.ms` that verifies no
   `__mscache__/script.msc` is created.
2. Add a failing CLI test for first execution creating
   `__mscache__/script.msc`.
3. Add a failing CLI test for second execution preserving output when source is
   unchanged and cache is valid.
4. Add a failing CLI test for corrupt cache fallback during `.ms` execution.
5. Add a failing CLI test proving `-e` does not create disk cache.
6. Implement CLI parsing and source-loader wiring.

## Acceptance

1. `mslangc script.ms` creates and reuses a valid cache.
2. `mslangc --no-cache script.ms` reads no cache and writes no cache.
3. `mslangc -e "code"` behaves as before and creates no cache.
4. Corrupt automatic `.msc` files do not produce user-visible diagnostics for
   source execution and do not prevent successful source execution.
5. Existing CLI usage errors remain stable.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "mscache|cli"
```

## Out of Scope

1. Imported module cache policy.
2. Direct `.msc` execution.
3. Source-free execution.
4. Verbose cache diagnostics.
