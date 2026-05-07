# Task 01 - Cache Format Foundation

## Status

Completed.

## Goal

Add the minimal disk-cache format foundation that can be built and unit-tested
without changing CLI execution behavior.

## Design Links

- Cache architecture and file format:
  [msc-cache-design.md](msc-cache-design.md)
- Language and runtime baseline:
  [../../mslangc-design.md](../../mslangc-design.md)
- Repository rules:
  [../../AGENTS.md](../../AGENTS.md)

## Dependencies

1. Existing build and unit-test harness.
2. Existing runtime chunk, value, string, and compiler ABI definitions.

## Scope

1. Define `.msc` file-format constants: magic, format version, compiler/cache
   ABI version, entry-kind values, and fixed-width integer field sizes.
2. Add a source metadata model with display path, byte size, and modification
   timestamp.
3. Add endian-stable primitive read/write helpers for unsigned integers,
   signed integers where needed, and IEEE-754 double values.
4. Add cache path derivation from `path/to/name.ms` to
   `path/to/__mscache__/name.msc`.
5. Add focused unit tests for constants, primitive encoding, metadata storage,
   and path derivation.

## Implementation Boundaries

1. Do not serialize or deserialize `MsChunk` in this task.
2. Do not read or write `.msc` files from disk in this task.
3. Do not change `src/main.c`, module import execution, or VM load behavior.
4. Keep the file format explicit and independent from C struct layout.
5. Use fixed-width integer types from `<stdint.h>` for all on-disk fields.

## File Ownership

1. Create `include/ms/cache/cache_format.h`.
2. Create `src/cache/cache_format.c`.
3. Modify `CMakeLists.txt` or the existing source list to compile the new
   cache source file.
4. Create `tests/unit/cache_format_test.c`.
5. Modify `tests/unit/CMakeLists.txt` to register `cache_format.unit`.

## TDD Plan

1. Add failing tests that assert magic bytes, version constants, and entry-kind
   constants.
2. Add failing tests for little-endian round trips of `uint16_t`, `uint32_t`,
   `uint64_t`, and `double`.
3. Add failing tests for deriving cache paths from simple, nested, and
   extension-edge source paths.
4. Implement only the constants and helper functions needed to pass those
   tests.
5. Run the focused unit test after each helper group is implemented.

## Acceptance

1. `cache_format.unit` passes.
2. The project builds with the new cache source file included.
3. Cache path derivation returns `__mscache__/name.msc` beside the source file.
4. Primitive encoding tests prove that file bytes are stable and do not depend
   on host struct layout.
5. No CLI, VM, import, or compiler behavior changes in this task.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "cache_format\.unit"
```

## Out of Scope

1. Chunk payload serialization.
2. Atomic file writing.
3. Cache invalidation.
4. CLI flags.
5. Direct `.msc` execution.
