# T14: Compiler — Basic

**Phase**: 6 · **Deps**: T12 (Bytecode Chunk), T13 (Parser) · **Complexity**: High

## Goal

Single-pass compiler: walk AST → emit bytecode. Covers expressions, variables, control flow. Functions/classes/closures → later tasks.

## Files

| File | Purpose |
|------|---------|
| `src/compiler.h` | `MsCompiler` struct, state, API |
| `src/compiler.c` | Impl (basic subset) |

## TDD Cycles

### Cycle 1: Compiler Init + Literal Expressions

**RED**: `ms_compiler_init`, `ms_compiler_free`, `ms_compiler_compile` undefined → link error.

- `test_compiler_init_free()`: init + free, no crash/leak
- `test_compile_number()`: compile `"42"` → `MS_OP_CONSTANT(42)`, `MS_OP_POP`, `MS_OP_RETURN`
- `test_compile_bool()`: `"true"` → `MS_OP_TRUE`; `"false"` → `MS_OP_FALSE`; `"nil"` → `MS_OP_NIL`
- `test_compile_string()`: `"\"hello\""` → `MS_OP_CONSTANT` pointing to string value

**GREEN**: Create `src/compiler.h`:
```c
#ifndef MS_COMPILER_H
#define MS_COMPILER_H

#include "chunk.h"
#include "parser.h"

typedef enum { MS_FUNC_SCRIPT, MS_FUNC_FUNCTION, MS_FUNC_METHOD, MS_FUNC_INITIALIZER } MsFunctionType;

typedef struct MsLocal { MsToken name; int depth; bool isCaptured; } MsLocal;
typedef struct MsCompilerUpvalue { int index; bool isLocal; } MsCompilerUpvalue;

typedef struct MsCompilerState {
    struct MsCompilerState* enclosing;
    MsFunction* function;
    MsFunctionType type;
    MsLocal locals[MS_MAX_LOCALS];
    int localCount;
    int scopeDepth;
    MsCompilerUpvalue upvalues[MS_MAX_UPVALUES];
    int upvalueCount;
    int* breakJumps; int breakJumpCount; int breakJumpCapacity;
    int* continueJumps; int continueJumpCount; int continueJumpCapacity;
    int loopStart;
} MsCompilerState;

typedef struct { MsParser parser; MsCompilerState* current; } MsCompiler;

void ms_compiler_init(MsCompiler* compiler);
void ms_compiler_free(MsCompiler* compiler);
MsFunction* ms_compiler_compile(MsCompiler* compiler, const char* source);
void ms_compiler_mark_roots(MsCompiler* compiler);

#endif
```

- Update `src/object.h`/`src/object.c`: fully implement `MsFunction` with `MsChunk chunk` field.
- Create `src/compiler.c`:
  - `ms_compiler_init()`: zero-init
  - `ms_compiler_free()`: free state chain, close parser
  - `ms_compiler_compile()`: init parser → parse → init compiler state → create top-level `MsFunction` (`MS_FUNC_SCRIPT`) → walk AST compiling each stmt → emit `MS_OP_RETURN` → return compiled function
  - Emit helpers: `emitByte()`, `emitBytes()`, `emitConstant()`, `emitReturn()`
  - `makeConstant()`: add value to constant pool, return index
  - `compileExpr()`: dispatch on type — `MS_EXPR_LITERAL` → `MS_OP_CONSTANT`/`MS_OP_NIL`/`MS_OP_TRUE`/`MS_OP_FALSE`
  - `compileStmt()`: `MS_STMT_EXPR` → compile expr + `MS_OP_POP`

**Verify GREEN**: `cmake --build build && ./build/test_compiler`

**REFACTOR**: Ensure `MsFunction` freed via `ms_object_free()` chain.

### Cycle 2: Unary + Binary Arithmetic

**RED**: Unary/binary expressions not compiled → wrong bytecode.

- `test_compile_unary_negate()`: `"-42"` → `OP_CONSTANT(42)`, `OP_NEGATE`, `OP_POP`, `OP_RETURN`
- `test_compile_unary_not()`: `"!true"` → `OP_TRUE`, `OP_NOT`, `OP_POP`, `OP_RETURN`
- `test_compile_add()`: `"1 + 2"` → `OP_CONSTANT(1)`, `OP_CONSTANT(2)`, `OP_ADD`, `OP_POP`, `OP_RETURN`
- `test_compile_arithmetic()`: `"1 + 2 * 3 - 4 / 5 % 6"` → all ops in correct order

**GREEN**: Extend `compileExpr()`:
- `MS_EXPR_UNARY`: compile operand → emit `MS_OP_NEGATE` (`-`) or `MS_OP_NOT` (`!`)
- `MS_EXPR_BINARY`: compile left, right → emit opcode (`ADD`/`SUBTRACT`/`MULTIPLY`/`DIVIDE`/`MODULO`)
- Mapping: `+`→`ADD`, `-`→`SUBTRACT`, `*`→`MULTIPLY`, `/`→`DIVIDE`, `%`→`MODULO`

**Verify GREEN**: build + run → arithmetic tests pass.

**REFACTOR**: Lookup table or switch for operator→opcode mapping.

### Cycle 3: Comparison, Equality, Logical Expressions

**RED**: Wrong opcodes or missing jump instructions.

- `test_compile_comparison()`: `"1 < 2"` → `OP_CONSTANT(1)`, `OP_CONSTANT(2)`, `OP_LESS`
- `test_compile_equality()`: `"1 == 2"` → `OP_EQUAL`; `"1 != 2"` → `OP_EQUAL`, `OP_NOT`
- `test_compile_logical_and()`: `"true and false"` → short-circuit: `OP_TRUE`, `OP_JUMP_IF_FALSE`, `OP_POP`, `OP_FALSE`
- `test_compile_logical_or()`: `"true or false"` → short-circuit jump pattern

**GREEN**:
- Comparison: emit `LESS`/`GREATER`/`LESS_EQUAL`/`GREATER_EQUAL`/`EQUAL`; `!=` → `EQUAL` + `NOT`
- Implement `emitJump()` + `patchJump()` for forward jumps
- `MS_EXPR_LOGICAL`:
  - `and`: compile left → `OP_JUMP_IF_FALSE` → `OP_POP` → compile right
  - `or`: compile left → `OP_JUMP_IF_TRUE` → `OP_POP` → compile right
- Jump offsets: 2-byte signed

**Verify GREEN**: build + run → comparison/logical tests pass.

**REFACTOR**: Verify jump offset calculation correct for all cases.

### Cycle 4: Global Variables

**RED**: Variable opcodes not emitted.

- `test_compile_var_decl()`: `"var x = 42"` → `OP_CONSTANT(42)`, `OP_DEFINE_GLOBAL("x")`
- `test_compile_var_decl_no_init()`: `"var x"` → `OP_NIL`, `OP_DEFINE_GLOBAL("x")`
- `test_compile_get_global()`: `"print x"` → `OP_GET_GLOBAL("x")`
- `test_compile_set_global()`: `"x = 42"` → `OP_CONSTANT(42)`, `OP_SET_GLOBAL("x")`
- `test_compile_undefined_var()`: `"print undefined_var"` → compile error
- `test_compile_redeclare()`: `"var x = 1\nvar x = 2"` → compile error

**GREEN**:
- `identifierConstant()`: add lexeme string to constant pool, return index
- `MS_STMT_VAR_DECL`: compile init (or `OP_NIL`) → `OP_DEFINE_GLOBAL` with name index
- `MS_EXPR_VARIABLE` → `OP_GET_GLOBAL`; `MS_EXPR_ASSIGN` → compile value + `OP_SET_GLOBAL`
- Undefined var: error at compile time (locals) or deferred to runtime (globals)

**Verify GREEN**: build + run → variable tests pass.

**REFACTOR**: Consolidate identifier constant lookup (reuse existing same-string constant).

### Cycle 5: Local Variables + Scoping

**RED**: Global ops instead of local, or missing scoping.

- `test_compile_local_var()`: `"{ var x = 1\nprint x }"` → local slot 0, no global ops
- `test_compile_scope_depth()`: nested blocks → correct slot indices + scope behavior
- `test_compile_set_local()`: `"{ var x = 1\nx = 2 }"` → `OP_SET_LOCAL`
- `test_compile_error_read_own_init()`: `"var x = x"` → compile error

**GREEN**:
- `beginScope()`: increment `scopeDepth`
- `endScope()`: decrement, emit `OP_POP` per out-of-scope local
- `declareVariable()`: add local with current scopeDepth
- `defineVariable()`: mark initialized (`depth = scopeDepth`)
- `resolveLocal()`: walk locals backward → slot index (-1 if not found, error if in own init)
- If `resolveLocal()` finds var → `OP_GET_LOCAL`/`OP_SET_LOCAL` instead of global ops
- `MS_STMT_BLOCK`: `beginScope()` → compile stmts → `endScope()`

**Verify GREEN**: build + run → local/scope tests pass.

**REFACTOR**: Verify slot indices across nested scopes.

### Cycle 6: If/Else Statements

**RED**: Wrong jump offsets or missing jumps.

- `test_compile_if()`: `"if (true) print 1"` → `OP_TRUE`, `OP_JUMP_IF_FALSE(then)`, `OP_POP`, body, `OP_JUMP(end)`, patch then, `OP_POP`
- `test_compile_if_else()`: `"if (true) print 1 else print 2"` → both branches with correct offsets
- `test_compile_nested_if()`: nested chain → all jumps patched correctly

**GREEN**: Compile `MS_STMT_IF`:
1. Compile condition
2. `MS_OP_JUMP_IF_FALSE` (then jump, save to patch)
3. `MS_OP_POP`
4. Compile then-branch
5. `MS_OP_JUMP` (else jump, save to patch)
6. Patch then-jump
7. `MS_OP_POP` (false path)
8. If else exists, compile it
9. Patch else-jump

`emitJump()` returns offset, `patchJump()` writes destination.

**Verify GREEN**: build + run → if/else tests pass.

**REFACTOR**: Verify 2-byte signed offset arithmetic consistent.

### Cycle 7: While/For Loops + Break/Continue

**RED**: Wrong loop offsets, break/continue not patched.

- `test_compile_while()`: `"while (true) print 1"` → loop start, condition, exit jump, body, `OP_LOOP` back, exit patch
- `test_compile_for()`: `"for (var i = 0; i < 10; i = i + 1) print i"` → decl, condition, body, increment, loop back, exit
- `test_compile_break()`: `"while (true) break"` → `OP_JUMP` to loop exit
- `test_compile_continue()`: `"while (true) continue"` → `OP_JUMP` to loop start (or increment for for)
- `test_compile_nested_loops()`: break/continue target correct loop

**GREEN**:
- `MS_STMT_WHILE`:
  1. Record `loopStart`
  2. Compile condition
  3. `OP_JUMP_IF_FALSE` (exit)
  4. `OP_POP`
  5. Compile body (loop context for break/continue)
  6. `OP_LOOP` back to `loopStart`
  7. Patch exit
  8. `OP_POP` (false branch)
- `MS_STMT_FOR`:
  1. `beginScope()`
  2. Compile init (var decl or expr)
  3. Record `loopStart`
  4. Compile condition + exit jump (or infinite loop if absent)
  5. Compile body
  6. Record increment offset; compile increment + `OP_POP`
  7. `OP_LOOP` back to `loopStart`
  8. Patch exit
  9. `endScope()`
- Break: `OP_JUMP`, record in breakJumps, patch all at loop exit
- Continue: `OP_JUMP` to increment start (for) or loop start (while)
- `emitLoop()`: `OP_LOOP` with offset back to given instruction

**Verify GREEN**: build + run → all loop tests pass.

**REFACTOR**: Factor out loop-context (break/continue jump lists) into helpers.

### Cycle 8: Return + Expression Statements

**RED**: Return/print not handled.

- `test_compile_return_value()`: `"return 42"` → `OP_CONSTANT(42)`, `OP_RETURN`
- `test_compile_return_nil()`: `"return"` → `OP_NIL`, `OP_RETURN`
- `test_compile_return_at_top_level()`: `"return 1"` at top level → compile error
- `test_compile_expr_stmt()`: `"1 + 2"` → computed + `OP_POP`
- `test_compile_print()`: `"print 1 + 2"` → expression + `OP_PRINT`

**GREEN**:
- `MS_STMT_RETURN`: top-level (`MS_FUNC_SCRIPT`) → error; compile value (or `OP_NIL`) → `OP_RETURN`
- `MS_STMT_EXPR`: compile expr → `OP_POP`
- Print: `print` keyword → compile expr → `OP_PRINT` instead of `OP_POP`
- `MS_STMT_BREAK` / `MS_STMT_CONTINUE`: emit jumps (Cycle 7); outside loop → compile error

**Verify GREEN**: build + run → all tests pass.

**REFACTOR**: Add stubs for `compileFuncDecl`/`compileClassDecl`/`compileImportStmt` → "not yet implemented" error.

## Acceptance Criteria

- [ ] `"print 1 + 2"` → `OP_CONSTANT`, `OP_ADD`, `OP_PRINT`
- [ ] `"var x = 42; print x"` → `OP_CONSTANT`, `OP_DEFINE_GLOBAL`, `OP_GET_GLOBAL`
- [ ] `"if (true) print 1 else print 2"` → `OP_TRUE`, `OP_JUMP_IF_FALSE`, …
- [ ] `"while (true) print 1"` → `OP_LOOP`
- [ ] `"{ var x = 1; print x }"` → local variable with scope depth
- [ ] `"for (var i = 0; i < 10; i = i + 1) print i"` → for loop bytecode
- [ ] `"break"` / `"continue"` → correct jump patching
- [ ] Error on undefined variable access
- [ ] Error on redeclare in same scope

## Notes

- Static fns: `emitByte`, `emitBytes`, `emitLoop`, `emitJump`, `patchJump`, `emitReturn`, `emitConstant`, `makeConstant`, `identifierConstant`, `declareVariable`, `defineVariable`, `markInitialized`, `resolveLocal`, `beginScope`, `endScope`, `compileExpr`, `compileStmt`, `compileBlock`, `compileVarDecl`, `compileIfStmt`, `compileWhileStmt`, `compileForStmt`, `compileReturnStmt`, `compileBreakStmt`, `compileContinueStmt`, `compileExprStmt`
- Stubs (error): `compileFuncDecl` (T16), `compileClassDecl` (T18), `compileImportStmt` (T20)
- `ms_compiler_mark_roots()` — GC integration, marks compiler-allocated objects as reachable
