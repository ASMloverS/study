# Task 04 - Cache File I/O and Validation

## Status

Done.

## Goal

Read, validate, and atomically write complete `.msc` files around serialized
chunk payloads.

## Design Links

- Header, validity, and write policy:
  [msc-cache-design.md](msc-cache-design.md)

## Dependencies

1. Task 03 - Function Graph Serialization.
2. Existing platform file utilities, or small new C helpers if no shared file
   utility exists.

## Scope

1. Define the complete header encoding with magic, format version, ABI version,
   source display path, source size, source modification time, entry kind,
   payload offset, and payload length.
2. Add `.msc` file reader validation for header fields, payload bounds, version
   compatibility, and structural payload integrity.
3. Add source-metadata validation for automatic `.ms` cache loading using
   `mtime + size`.
4. Add writer support that creates `__mscache__/`, writes a temporary file in
   that directory, closes it, and renames it over the final `.msc` path.
5. Add tests for valid files, stale metadata, corrupt headers, truncated
   payloads, incompatible versions, and atomic replacement behavior.

## Implementation Boundaries

1. Source `.ms` execution must treat cache read and write failures as soft
   failures later, but this task only exposes statuses and error categories.
2. Direct `.msc` hard-failure policy is not wired to the CLI in this task.
3. Do not compile source code or execute deserialized chunks here.
4. Do not add verbose user diagnostics for automatic `.ms` cache corruption.
5. Avoid platform-specific assumptions beyond the repository's supported build
   environment.

## File Ownership

1. Create `include/ms/cache/cache_file.h`.
2. Create `src/cache/cache_file.c`.
3. Modify `CMakeLists.txt` or the existing source list to compile the new file.
4. Create `tests/unit/cache_file_test.c`.
5. Modify `tests/unit/CMakeLists.txt` to register `cache_file.unit`.
6. Create temporary test data under the build tree, not under source-controlled
   fixture directories.

## TDD Plan

1. Add a failing test that writes a valid cache file and reads it back.
2. Add failing tests for stale source size and stale source modification time.
3. Add failing tests for bad magic, bad format version, bad ABI version,
   payload offset outside file bounds, and truncated payload.
4. Add a failing test proving atomic replacement overwrites an older valid
   cache only after the new temporary file is complete.
5. Implement reader and writer behavior in small increments.

## Acceptance

1. `cache_file.unit` passes.
2. Valid `.msc` files round-trip through disk and reconstruct the payload chunk.
3. Invalid automatic source-cache candidates are rejected without crashing.
4. Writer creates `__mscache__/` and replaces stale cache files atomically.
5. Error categories are explicit enough for later source fallback and direct
   `.msc` hard failure handling.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "cache_format\.unit|chunk_codec\.unit|cache_file\.unit"
```

## Out of Scope

1. CLI option parsing.
2. VM execution of deserialized chunks.
3. Import behavior.
4. Debug or verbose cache diagnostics.
