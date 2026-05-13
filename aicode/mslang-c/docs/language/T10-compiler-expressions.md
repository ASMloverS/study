# Task 10: Compiler — Expressions

**Status:** DONE

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement single-pass Pratt parser for expressions: literals, arithmetic, comparisons, logical and/or, unary, bitwise, grouping. Uses ExprDesc to minimize register moves.
**Dependencies:** T05, T06, T08
**Produces:** `ms_compile()` compiles expressions to bytecode; disassembler verifies output

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/compiler.h` | Public compiler interface |
| Create | `src/compiler_impl.h` | Internal structures (MsCompiler, ExprDesc, Local, etc.) |
| Create | `src/compiler.c` | Core compile logic, advance/consume/emit |
| Create | `src/compiler_expr.c` | Expression parsing functions |
| Modify | `CMakeLists.txt` | Add `ms_frontend` library |
| Create | `tests/unit/test_compiler_expr.c` | Expression compile tests |

## Key Data Structures / API

```c
// include/ms/compiler.h
#pragma once
#include "ms/object.h"

typedef struct {
    int line, column;
    char message[256];
} MsDiagnostic;

typedef struct MsVM MsVM;

// Compile source to top-level function; returns NULL on error
MsObjFunction* ms_compile(MsVM* vm, const char* source, const char* path,
                           MsDiagnostic* diags, int* diag_count, int max_diags);
```

```c
// src/compiler_impl.h (internal header)
typedef enum {
    EDESC_VOID, EDESC_NIL, EDESC_TRUE, EDESC_FALSE,
    EDESC_NUMBER, EDESC_INT, EDESC_CONST,
    EDESC_REG, EDESC_GLOBAL, EDESC_LOCAL, EDESC_UPVAL, EDESC_PROP,
} MsExprKind;

typedef struct {
    MsExprKind kind;
    union {
        double number;
        ms_i64 integer;
        int idx;
        struct { int obj_reg; int key_const; } prop;
    };
    int true_list;   // JMP patch chain for true
    int false_list;  // JMP patch chain for false
} MsExprDesc;

#define NO_JUMP (-1)

typedef struct {
    MsToken name;
    int depth;
    bool is_captured;
    int slot;
} MsLocal;

typedef struct {
    bool is_local;
    int index;
} MsUpvalueDesc;

typedef struct MsLoopCtx {
    int start;
    int break_list;
    int depth;
    struct MsLoopCtx* enclosing;
} MsLoopCtx;

typedef struct MsClassCompiler {
    struct MsClassCompiler* enclosing;
    bool has_superclass;
} MsClassCompiler;

typedef void (*MsParseFn)(struct MsCompiler*, bool can_assign);

typedef enum {
    PREC_NONE = 0, PREC_ASSIGNMENT, PREC_TERNARY, PREC_OR, PREC_AND,
    PREC_EQUALITY, PREC_COMPARISON, PREC_BIT_OR, PREC_BIT_XOR,
    PREC_BIT_AND, PREC_SHIFT, PREC_TERM, PREC_FACTOR,
    PREC_UNARY, PREC_CALL, PREC_PRIMARY,
} MsPrecedence;

typedef struct {
    MsParseFn prefix;
    MsParseFn infix;
    MsPrecedence precedence;
} MsParseRule;

typedef struct MsCompiler {
    MsScanner scanner;
    MsToken current;
    MsToken previous;
    bool had_error;
    bool panic_mode;
    MsDiagnostic* diags;
    int* diag_count;
    int max_diags;
    MsVM* vm;
    MsObjFunction* function;
    struct MsCompiler* enclosing;
    MsLocal locals[256];
    int local_count;
    int scope_depth;
    MsUpvalueDesc upvalues[MS_MAX_UPVALUES];
    int next_reg;
    int max_reg;
    MsLoopCtx* loop;
    MsClassCompiler* klass;
    MsTable string_cache;  // string dedup: ObjString* → int(const idx)
} MsCompiler;
```

## Implementation Notes

- **Pratt Parser**: `parse_precedence(c, min_prec)` calls the prefix function, then loops calling infix functions while the current token's precedence ≥ `min_prec`
- **ExprDesc**: deferred evaluation descriptor. `EDESC_NUMBER/INT` does not emit `LOADK` immediately — only when materialized into a register, enabling constant folding
- **`expr_to_reg(c, e)`**: materializes an `ExprDesc` into a register: `NIL`→`LOADNIL`, `TRUE`→`LOADTRUE`, `NUMBER`→`LOADK`, `REG`→no-op, `GLOBAL`→`GETGLOBAL`, etc.
- **Constant folding**: for binary `+,-,*,/,%`, if both sides are `EDESC_NUMBER/INT`, evaluate at compile time. e.g. `1 + 2` → `EDESC_INT(3)`
- **Logical and/or**: short-circuit via `TEST` + `JMP`. `and`: if left is false → skip right; `or`: if left is true → skip right

Core functions:
```c
static void advance(MsCompiler* c);
static void consume(MsCompiler* c, MsTokenType type, const char* msg);
static int  emit(MsCompiler* c, MsInstruction instr);
static int  alloc_reg(MsCompiler* c);
static void free_reg(MsCompiler* c, int reg);
static int  expr_to_reg(MsCompiler* c, MsExprDesc* e);
static int  expr_to_any_reg(MsCompiler* c, MsExprDesc* e);
static int  add_constant(MsCompiler* c, MsValue val);
static MsExprDesc parse_precedence(MsCompiler* c, MsPrecedence min);
static MsExprDesc expression(MsCompiler* c);
```

## C Unit Tests

```c
// tests/unit/test_compiler_expr.c
#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/vm.h"

static void test_constant_folding(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(1 + 2)", "<test>",
                                    diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    // 1+2 should be folded to constant 3
    // Expect: LOADK (for 3), then GETGLOBAL/CALL for print
    ms_disasm_chunk(&fn->chunk, "constant_fold");
    ms_vm_free(&vm);
}

static void test_unary(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(-5)", "<test>",
                                    diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "unary");
    ms_vm_free(&vm);
}

int main(void) {
    test_constant_folding();
    test_unary();
    printf("test_compiler_expr: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/expressions.ms (run after T13)
print(1 + 2)
// expect: 3
print(10 - 3)
// expect: 7
print(2 * 6)
// expect: 12
print(10 / 4)
// expect: 2.5
print(10 % 3)
// expect: 1
print(-5)
// expect: -5
print(!true)
// expect: false
print(!nil)
// expect: true
print(1 < 2)
// expect: true
print(2 > 1)
// expect: true
print(1 == 1)
// expect: true
print(1 != 2)
// expect: true
print(1 <= 1)
// expect: true
print(true and false)
// expect: false
print(false or true)
// expect: true
print(nil or "default")
// expect: default
print("a" + "b")
// expect: ab
print(1 << 3)
// expect: 8
print(15 & 9)
// expect: 9
print(~0)
// expect: -1
```
