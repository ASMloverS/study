# Task 03 - Function Graph Serialization

## Status

Complete. Verified on 2026-05-08 with the acceptance commands in this document.

## Goal

Extend chunk serialization so `.msc` payloads preserve the complete executable
object graph, including nested function chunks.

## Design Links

- Nested function serialization requirement:
  [msc-cache-design.md](msc-cache-design.md)
- Function, closure, and bytecode baseline:
  [../../mslangc-design.md](../../mslangc-design.md)

## Dependencies

1. Task 02 - Chunk Constant Serialization.
2. Existing `MsFunction` object layout and allocation helpers.
3. Existing compiler output for functions, methods, and closures.

## Scope

1. Add an explicit function constant tag to the payload format.
2. Serialize function name, arity, upvalue count, flags, and nested chunk.
3. Deserialize function constants through normal runtime allocation.
4. Add recursive structural validation for nested chunks and function payloads.
5. Add tests for named, anonymous, and nested function constants where
   supported.

## Implementation Boundaries

1. Do not serialize closure runtime state; cache files store compiled function
   metadata and chunks only.
2. Do not preserve object addresses from the writer process.
3. Reject unsupported or runtime-only constants rather than guessing a format.
4. Keep recursion bounded enough that a corrupt payload cannot cause unbounded
   reader recursion.
5. Do not integrate cache hits into execution yet.

## File Ownership

1. Modify `include/ms/cache/chunk_codec.h`.
2. Modify `src/cache/chunk_codec.c`.
3. Modify `tests/unit/chunk_codec_test.c`.
4. Add focused fixture scripts under `tests/fixtures/cache/` only if compiling
   real source is useful for constructing representative chunks.

## TDD Plan

1. Add a failing test for round-tripping a function constant with a name, arity,
   upvalue count, flags, and an empty nested chunk.
2. Add a failing test for a function whose nested chunk contains bytecode and
   primitive constants.
3. Add a failing test for a nested function inside another function chunk.
4. Add a failing test for rejecting excessive function nesting or malformed
   nested payload bounds.
5. Implement recursive function serialization only after the failing tests are
   registered.

## Acceptance

1. `chunk_codec.unit` passes with primitive and function constant coverage.
2. Nested `MsFunction.chunk` graphs round-trip without sharing storage with the
   original graph.
3. Function metadata needed by the VM is preserved exactly.
4. Malformed nested function payloads fail cleanly.
5. The codec still rejects unsupported constants safely.

## Acceptance Commands

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "chunk_codec\.unit"
```

## Out of Scope

1. Disk file read/write.
2. CLI execution of cache files.
3. Import cache integration.
4. Optimized cache payloads or compression.
