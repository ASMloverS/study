# mslangc Disk Instruction Cache Design

## Summary

`mslangc` currently compiles source on every process run. The CLI entry path
loads `.ms` source, calls `ms_compile_source()`, and executes the resulting
`MsChunk`. Imported modules follow the same source-read and compile path inside
the VM, with only an in-process `MsModuleCache` preventing repeated module
initialization during a single run.

This design adds a Python-style disk instruction cache. The first execution of a
`.ms` file writes a binary instruction file under a sibling `__mscache__/`
directory. Later executions load the cached instruction file when it is valid,
avoiding parse, resolve, and lowering work.

The first version uses one cache file per source file:

```text
path/to/foo.ms
path/to/__mscache__/foo.msc
```

The cache applies to both CLI entry scripts and imported modules. Inline source
from `-e` is not cached.

## Current Implementation Facts

- `src/main.c` reads entry script source and calls `ms_compile_source()` on every
  file execution.
- `src/runtime/vm.c` reads imported module source and calls
  `ms_compile_source()` during the first import of a module in a VM instance.
- `MsModuleCache` avoids duplicate module initialization only inside one VM
  process.
- `MsChunk` stores bytecode, line data, and constants.
- Function and method bytecode is nested through `MsFunction.chunk` objects in
  the constant graph, so a cache file must serialize the complete executable
  object graph, not only the top-level bytecode bytes.

## Behavior

- `mslangc script.ms` enables cache by default.
- `mslangc --no-cache script.ms` skips cache reads and writes for the entry
  script and all imported modules.
- `mslangc -e "code"` keeps the current direct compile-and-run behavior and does
  not use disk cache.
- `mslangc __mscache__/script.msc` directly executes the cached instruction file
  without requiring the original `.ms` source file.
- Imported modules use the same cache policy as the entry script.
- Cache read, cache write, and cache directory creation failures are soft
  failures when executing `.ms` source: the runtime falls back to compiling
  source and continues execution.
- Direct `.msc` execution treats an invalid, incompatible, unreadable, or
  corrupt `.msc` as a hard failure because source may not exist.

## Cache Validity

Automatic `.ms` cache loading validates:

- cache magic
- cache format version
- compiler/cache ABI version
- source file size
- source file modification time
- payload bounds and basic structural integrity

If any check fails, the loader discards the cache for that run, recompiles the
`.ms` source, and attempts to replace the stale `.msc`.

The first version uses `mtime + size` invalidation. This is intentionally fast
and avoids reading the whole source file on a cache hit. The known tradeoff is
that a same-size rewrite within the platform timestamp granularity can be missed.
A future hash mode can close that gap if needed.

Direct `.msc` execution does not validate source metadata and does not require
the original source file to exist.

## File Format

The `.msc` file is a stable binary format, not a dump of C structs.

The header contains:

- ASCII magic, for example `MSLCMSC\0`
- format version, starting at `1`
- compiler/cache ABI version
- source display path
- source size
- source modification time with the best available platform precision
- entry kind
- payload offset
- payload length

The payload contains a serialized top-level `MsChunk`.

Chunk serialization includes:

- bytecode bytes
- line table
- constant table

Constant serialization uses explicit file-format tags and supports the compile
time constants needed by current bytecode:

- nil
- bool
- number
- string
- function

Function serialization includes:

- optional name
- arity
- upvalue count
- flags
- nested chunk

The reader reconstructs runtime objects through normal allocation paths or
equivalent initialization code. It must not reuse addresses from the file.

If the writer encounters a constant that cannot be serialized safely, writing
the cache fails but source execution continues.

## Loading Architecture

Add a shared load service used by both CLI entry execution and VM module import.
This avoids separate cache behavior in `src/main.c` and `src/runtime/vm.c`.

The shared source path load flow is:

1. Derive `__mscache__/name.msc` from the `.ms` path.
2. If cache is enabled, `stat()` the source and try to load a valid `.msc`.
3. If the cache is valid, return the reconstructed `MsChunk`.
4. Otherwise read `.ms`, compile with `ms_compile_source()`, and execute the
   compiled chunk.
5. After a successful compile, attempt to write the `.msc` atomically.

The direct `.msc` load flow is:

1. Read and validate the `.msc` header and payload.
2. Reconstruct the executable chunk and runtime object graph.
3. Execute the chunk using the existing VM entry frame path.

Imported module loading keeps the current `MsModuleState` transitions:

```text
UNSEEN -> INITIALIZING -> INITIALIZED
UNSEEN/INITIALIZING -> FAILED on error
```

Disk cache must not bypass cycle detection, import failure diagnostics, or
module namespace isolation.

## Import Behavior From Direct `.msc`

When directly executing an `.msc`, imports use the normal module name mapping
and search roots, with this additional preference:

1. Resolve the module to the corresponding `.ms` logical path when possible.
2. Prefer the matching `__mscache__/module.msc` if it exists and is compatible.
3. Fall back to the `.ms` source path if present.

This supports both cache-backed development runs and source-free `.msc` entry
execution when dependency `.msc` files are present.

## CLI Interface

The user-visible interface changes are intentionally small:

```text
mslangc [--help] [--no-cache] [-e code] [script.ms|script.msc]
```

Rules:

- `--no-cache` only affects `.ms` source execution.
- `--no-cache` disables cache for imports as well as the entry script.
- `.msc` input is treated as direct instruction execution.
- Unknown options keep returning the existing usage error.

## Write Policy

Cache writing uses atomic replacement:

1. Ensure `__mscache__/` exists.
2. Write to a temporary file in the same directory.
3. Close the temporary file successfully.
4. Rename or replace it as `name.msc`.

If any step fails, the loader ignores the cache write failure and continues
running the compiled source.

## Diagnostics

- Source execution keeps current parse, resolve, module, and runtime diagnostics.
- Cache hit execution still reports diagnostics against the cached source display
  path stored in the header.
- Direct `.msc` execution reports diagnostics against the stored source display
  path when present, otherwise against the `.msc` path.
- `.ms` cache corruption should not produce user-visible diagnostics unless a
  debug or verbose cache mode is added later.
- Direct `.msc` corruption should produce a clear hard failure.

## Tests

Add focused tests for:

- writing and reading a simple chunk cache
- serializing strings, numbers, booleans, nil, and nested functions
- first `.ms` execution creates `__mscache__/script.msc`
- second `.ms` execution uses a valid cache and preserves output
- source `mtime + size` mismatch invalidates and rewrites cache
- corrupt `.msc` falls back to source execution for `.ms`
- corrupt `.msc` fails for direct `.msc` execution
- `--no-cache` skips cache reads and writes for entry scripts and imports
- imported modules generate and reuse their own `.msc` files
- direct `.msc` execution works after the original entry `.ms` is removed
- direct `.msc` imports prefer dependency `.msc` files and fall back to `.ms`
  when needed

The acceptance gate remains:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Assumptions

- First version implements `.msc` only; `.mso` is reserved for future object or
  optimized artifacts.
- Cache files use simple source-name mapping, not Python-style versioned names.
- ABI/version checks in the header protect against stale cache after opcode or
  format changes.
- Cache is a performance layer for `.ms` execution and must not make source
  execution less reliable.
- Direct `.msc` execution is a supported mode and does not require source files.
