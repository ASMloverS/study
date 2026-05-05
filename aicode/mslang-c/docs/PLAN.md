# mslang-c: Maple Scripting Language — C11 Port Plan

## Context

Port mslang (C++23, ~15K lines, 52 files) → pure C11. Arch: register-based VM + single-pass compiler (Pratt, no AST). Not mslangx-c (AST + stack VM).

## Project Structure

```
mslang-c/
  CMakeLists.txt
  include/ms/
    common.h        -- type aliases, platform macros, static_assert
    consts.h        -- stack/frame limits, GC thresholds
    opcode.h        -- OpCode enum, instruction encoding/decoding, RK helpers
    token.h         -- TokenType enum (X-macro), Token struct
    value.h         -- MsValue tagged union (optional NaN-boxing)
    object.h        -- MsObject header, all Obj* declarations
    table.h         -- hash table (ObjString* keys)
    vtable.h        -- ValueTable (Value keys, for ObjMap)
    chunk.h         -- MsChunk: instruction array + constant pool + RLE line info
    scanner.h       -- MsScanner state and API
    compiler.h      -- compile() entry, MsDiagnostic
    memory.h        -- mark_object, write_barrier, ObjectPool
    vm.h            -- MsVM state, interpret() API
    debug.h         -- disassembler
    module.h        -- module loader
    optimize.h      -- peephole_optimize()
    serializer.h    -- .msc serialization/deserialization
    shape.h         -- Shape (hidden classes) and InlineCache
  src/
    main.c
    common.c
    value.c
    object.c          -- create/destroy/stringify/trace for all objects
    table.c / vtable.c
    chunk.c
    scanner.c
    compiler.c        -- Pratt parser core, declarations, statements
    compiler_expr.c   -- expression parsing
    compiler_impl.h   -- compiler internals (Local, Upvalue, ExprDesc, ...)
    vm.c              -- dispatch loop, stack ops, GC trigger
    vm_call.c         -- function calls, invoke, coroutine resume
    vm_builtins.c     -- built-in type methods (string/list/map/tuple)
    vm_natives.c      -- native function registration
    vm_gc.c           -- mark, sweep, generational, incremental
    vm_import.c       -- module loading
    debug.c / memory.c / module.c / optimize.c / serializer.c / shape.c
  tests/
    CMakeLists.txt
    test_assert.h     -- minimal test macros
    unit/             -- per-module unit tests
    fixtures/         -- .ms integration scripts
    conformance/      -- golden-output tests
  cmake/
    MslangTesting.cmake
```

## C++ → C Design Decisions

| C++ | C11 |
|-----|-----|
| `std::variant<nil,bool,double,i64,Object*>` | `struct MsValue { MsValueType type; union { … } as; }` |
| `std::vector<T>` | `struct { T* data; int count; int capacity; }` + `MS_ARRAY_PUSH` |
| `std::unordered_map` | `MsTable` open-addressing + linear probing |
| Singleton VM | explicit `MsVM*` parameter |
| RAII | `_init()` / `_destroy()` pairs |
| `enum class` | `enum` + `MS_` prefix |
| member functions | free functions, first param is `self*` |
| templates | `MS_ARRAY_PUSH` macro or per-type instantiation |
| virtual dispatch (none) | already switch-on-type; port directly |

**Naming:** types `MsValue`, fns `ms_value_is_nil()`, enum vals `MS_VAL_NIL`, macros `MS_ARRAY_PUSH`, internal fns `snake_case`.

**Errors:** compile → `MsDiagnostic[]`; runtime → error codes; exceptions → handler stack (not `longjmp`).

**Dispatch:** `switch` default; computed goto (`&&label`) GCC/Clang; MSVC → switch fallback.

---

## Implementation Phases

### Phase 01 — Project Skeleton
- CMakeLists.txt (C11, strict warnings), dir structure
- `common.h` (aliases: `ms_u8`, `ms_u32`, `ms_i64`), `consts.h` (kSTACK_MAX=256, kFRAMES_MAX=64)
- `main.c` prints version; `test_assert.h`, `cmake/MslangTesting.cmake`
- **Test:** build ok, `mslang-c --version` smoke

### Phase 02 — Value System and Hash Table
- `MsValue` tagged union (nil/bool/double/i64/Object*); ctor/check/extract macros, truthiness, equality, stringify
- `MsTable` open-addr (ObjString* keys, 75% load, pow2 cap); `MS_ARRAY_PUSH` dynamic array macro
- **Test:** value create/type-check/truthiness; table insert/lookup/delete/tombstone
- **Ref:** `mslang/src/Value.hh`, `mslang/src/Table.hh`

### Phase 03 — Object System (Strings and Base Objects)
- `MsObject` header (type, is_marked, generation, age, next)
- `MsObjString` via C FAM (`char data[]`), FNV-1a hash, interning; `MsObjFunction`, `MsObjNative`, `MsObjUpvalue`, `MsObjClosure` (FAM upvalues)
- `ms_allocate_object` macro; `object_stringify`/`object_destroy`/`object_size` dispatch
- **Test:** string create/intern dedup, FAM sizing, hash collisions
- **Ref:** `mslang/src/Object.hh`

### Phase 04 — Chunk and Instruction Encoding
- `MsInstruction` (uint32_t), `MsOpCode` enum (70+ opcodes)
- iABC/iABx/iAsBx encode/decode fns, RK helpers
- `MsChunk`: instr array + const pool + SourceRun RLE line info
- `ms_disassemble_chunk` / `ms_disassemble_instruction`
- **Test:** encode/decode roundtrip, sBx offset correctness, disassembly output
- **Ref:** `mslang/src/Opcode.hh`, `mslang/src/Chunk.hh`

### Phase 05 — Scanner (Lexer)
- `MsScanner`: all token types, ASI, bracket-depth suppression, `${expr}` interp, int vs. float literals, keyword recognition, save/restore state
- **Test:** all operators/keywords, string escapes, nested interp, ASI boundaries
- **Ref:** `mslang/src/Scanner.hh`, `mslang/src/TokenTypes.hh`

### Phase 06 — Compiler Core (Single-Pass Pratt Parser)
- `MsCompiler` struct, reg allocator, `ExprDesc` system
- `MsParseRule` table (prefix/infix/precedence), precedence climbing
- Exprs: literals, unary, binary arith/cmp, logical `and`/`or`, grouping
- Vars: local declare/resolve, global define/get/set
- Stmts: `var`, `print`, expr stmts, block scope
- Const folding, string const dedup
- **Test:** compile → verify via disassembly, reg alloc, ExprDesc opt
- **Ref:** `mslang/src/CompilerImpl.hh`, `mslang/src/Compiler.cc`, `mslang/src/CompilerExpr.cc`

### Phase 07 — VM Core (Basic Execution)
- `MsVM` struct (stack, frames, global table, string intern table)
- Dispatch: LOADK/LOADNIL/LOADTRUE/LOADFALSE, MOVE, arith, cmp, unary, bitwise
- GETGLOBAL/SETGLOBAL/DEFGLOBAL, JMP/TEST/TESTSET
- CALL/RETURN (simple fns), string concat (ADD on strings), RK decoding, stack-trace error reporting
- **Test:** `print 1 + 2` → `3`, vars, conditionals, fn calls, type errors
- **Ref:** `mslang/src/VM.hh`, `mslang/src/VM.cc`

### Phase 08 — Closures, Upvalues, Control Flow
- OP_CLOSURE + FAM upvalue array, upvalue capture (local vs. passed), GETUPVAL/SETUPVAL, CLOSE; open upvalue chain
- `while`, C-style `for`, `for-in` (FORITER), `break`/`continue` (LoopContext + patch list), `switch`/`case`
- **Test:** closure capturing locals, upvalue closing, counter closure, nested loop break/continue
- **Ref:** `mslang/src/CompilerStmt.cc`

### Phase 09 — Garbage Collection
- Basic mark-sweep: root marking, gray-stack tracing, sweep
- Generational GC: young/old lists, nursery threshold (256 KB), minor/major, age promotion; remember set, write barrier
- ObjectPool slab allocator (Upvalue, BoundMethod)
- **Test:** alloc stress, cycle collection, write-barrier correctness, promotion, pool allocator
- **Ref:** `mslang/src/VMGC.cc`, `mslang/src/Memory.hh`

### Phase 10 — OOP (Classes, Instances, Inheritance)
- CLASS/INHERIT/METHOD/STATICMETH/GETTER/SETTER/ABSTMETH
- ObjClass (method table, lazy getter/setter/abstract init)
- ObjInstance: Shape layout (SBO 8 inline fields, overflow → heap), ObjBoundMethod
- GETPROP/SETPROP + EXTRAARG IC slot, GETSUPER, INVOKE, SUPERINV
- Shape transitions, polymorphic IC (4-entry PIC, megamorphic fallback)
- **Test:** class create, field read/write, method call, inheritance + super, Shape sharing, IC hits
- **Ref:** `mslang/src/Object.hh` (Shape, InlineCache)

### Phase 11 — Collections and Built-in Methods
- ObjList (dyn array), ObjMap (ValueTable), ObjTuple (immutable, hashable)
- NEWLIST/NEWMAP/NEWTUPLE, GETIDX/SETIDX; string interp execution
- Built-ins: string (len/upper/lower/split/trim/…), list (push/pop/sort/map/filter/…), map (keys/values/has/remove/…), tuple (len/contains)
- ObjStringBuilder, ObjFile, ObjWeakRef
- **Test:** each collection create/op/index, each built-in method, string interp
- **Ref:** `mslang/src/VMBuiltins.cc`

### Phase 12 — Exceptions, defer, Coroutines
- TRY/ENDTRY/THROW + exception handler stack, stack unwinding; DEFER + frame-level deferred closure buffer
- Generator fns (`fun*`): ObjCoroutine (independent stack/frames), YIELD/RESUME, state machine; default + rest params
- **Test:** try/catch basic/nested/cross-frame, defer exec order, generator yield/resume/exhaustion
- **Ref:** `mslang/src/VMCall.cc`

### Phase 13 — Module System and Native Functions
- IMPORT/IMPFROM/IMPALIAS, ObjModule + export table; path resolution, caching, cycle detection
- Native fns: clock, type, str, num, input, len, int, float, assert, …; ASCII char cache
- **Test:** import/from-import/alias, module isolation, cycle detection, each native fn
- **Ref:** `mslang/src/VMImport.cc`, `mslang/src/VMNatives.cc`

### Phase 14 — Peephole Optimizer and Quickening
- 5-pass peephole: (1) redundant MOVE elim, (2) LOADNIL merge, (3) dead code after RETURN/THROW, (4) LOADK+NEG fold, (5) MOVE+RETURN tail merge
- NOP compaction + jump patching
- Runtime quickening: arith specialization (ADD→ADD_II/ADD_FF/ADD_SS, …), deopt counter (3 failures → revert); computed goto dispatch (GCC/Clang)
- **Test:** before/after bytecode cmp per opt, quickening specialization, deopt revert
- **Ref:** `mslang/src/Optimize.cc`

### Phase 15 — Serializer, Incremental GC, Finishing
- `.msc` binary serialize/deserialize (FNV-1a hash, DFS post-order, auto-cache)
- Incremental marking (64 gray objs/work slice)
- Op overloading (`__add`, `__sub`, …), enum decls, ternary op, list comprehensions
- **Test:** serialize/deserialize roundtrip, cache hit, hash mismatch → recompile, incremental GC pause verify

---

## Deferred / Skipped Features

| Feature | Decision | Reason |
|---------|----------|--------|
| LSP server | deferred | large scope, orthogonal to core runtime |
| NaN-boxing | deferred to post-Phase 15 | use tagged union first, add as compile option later |
| Colored terminal output | deferred | cosmetic only |
| JSON parsing | skipped | only needed by LSP |

---

## Validation Strategy

1. Each phase: unit tests, `cmake --build build && ctest`.
2. Phase 07+: run `.ms` scripts end-to-end, compare expected output.
3. Phase 13+: reuse 77 test scripts from `mslang/tests/` as conformance tests.
4. Final: all tests pass + valgrind/ASAN clean.

---

## Key Reference Files

- `mslang/src/Opcode.hh` — opcode defs, instr encoding
- `mslang/src/CompilerImpl.hh` — compiler internals
- `mslang/src/Object.hh` — 16 obj types, Shape, InlineCache
- `mslang/src/VM.hh` — full VM state
- `mslang/src/VMGC.cc` — generational incremental GC
- `mslang/src/Optimize.cc` — peephole optimizer
- `mslangx-c/CMakeLists.txt` — C11 CMake config ref

---

## Benchmarking

See `docs/BENCHMARK.md` for the full benchmark design rationale and phase breakdown.
See `benchmarks/README.md` for usage, case conventions, and regression thresholds.

The benchmark suite (`benchmarks/`) covers 13 micro-benchmark cases targeting individual VM subsystems. It requires `MSLANG_BUILD_BENCHMARKS=ON` and `MSLANG_VM_STATS=ON` at configure time and is excluded from the default build.

**Quick run:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DMSLANG_BUILD_BENCHMARKS=ON -DMSLANG_VM_STATS=ON
cmake --build build --config Release
python benchmarks/run_all.py --runs 5
```

**Baseline:** `benchmarks/baseline.json` was first captured in a Release + `MSLANG_VM_STATS=ON` build with 9 runs per case (`--with-cache`). It covers T28 (peephole optimizer), T29 (quickening + deopt), and T30 (incremental GC + `.msc` cache).

**Regression thresholds:** wall-clock ±5%, instruction count ±0.5%, `deopt_event_count` any absolute change — all trigger mandatory review.
