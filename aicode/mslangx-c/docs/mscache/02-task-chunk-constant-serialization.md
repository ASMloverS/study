# Task 02 - Chunk Constant Serialization

## Status

Complete. Verified on 2026-05-07 with the acceptance commands in this document.

## Goal

Serialize and deserialize top-level `MsChunk` payloads containing bytecode, line
data, and primitive constants.

## Design Links

- Cache payload requirements:
  [msc-cache-design.md](msc-cache-design.md)
- Bytecode and runtime object baseline:
  [../../mslangc-design.md](../../mslangc-design.md)

## Dependencies

1. Task 01 - Cache Format Foundation.
2. Existing `MsChunk` bytecode and constant-table support.
3. Existing runtime allocation paths for primitive constant reconstruction.

## Scope

1. Add payload writer helpers for chunk bytecode bytes.
2. Add payload writer helpers for line table data.
3. Add payload writer helpers for constant table entries with explicit tags for
   nil, bool, number, and string.
4. Add payload reader helpers that reconstruct a new `MsChunk` through normal
   runtime initialization and allocation paths.
5. Add unit tests that round-trip a manually built chunk and compare bytecode,
   line data, constant tags, and constant values.

## Implementation Boundaries

1. Do not serialize `MsFunction` constants in this task.
2. If the writer encounters an unsupported constant type, return a controlled
   serialization failure instead of writing partial payload data.
3. Do not introduce disk file I/O; tests should exercise payload buffers
   directly.
4. Do not execute deserialized chunks through the VM yet.
5. Keep reader validation strict for payload bounds and malformed constant tags.

## File Ownership

1. Create `include/ms/cache/chunk_codec.h`.
2. Create `src/cache/chunk_codec.c`.
3. Modify `CMakeLists.txt` or the existing source list to compile the new
   codec source file.
4. Create `tests/unit/chunk_codec_test.c`.
5. Modify `tests/unit/CMakeLists.txt` to register `chunk_codec.unit`.

## TDD Plan

1. Add a failing test for serializing and reading an empty chunk.
2. Add a failing test for bytecode and line table round-trip.
3. Add a failing test for nil, bool, number, and string constants.
4. Add a failing test for rejecting an unknown constant tag in an input buffer.
5. Implement the smallest codec surface that passes the tests.

## Acceptance

1. `chunk_codec.unit` passes.
2. A deserialized chunk has independent storage and does not alias the input
   chunk arrays.
3. Primitive constants round-trip exactly enough to preserve current VM
   behavior.
4. Malformed payloads fail cleanly without reading past the supplied buffer.
5. Unsupported constant writing fails without crashing.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "cache_format\.unit|chunk_codec\.unit"
```

## Out of Scope

1. Function and closure payloads.
2. Cache file headers.
3. Source metadata validation.
4. CLI and import integration.
