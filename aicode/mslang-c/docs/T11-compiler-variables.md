# Task 11: Compiler — Variables and Scoping

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Add variable declarations, local/global resolution, scoping with block statements, and compound assignment.
**Dependencies:** T10
**Produces:** Compiler supports `var` declarations, block scoping, assignment, and compound assignment

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/compiler.c` | Add `var` declaration, scope management, assignment |
| Modify | `src/compiler_expr.c` | Add identifier resolution (local/global) |
| Create | `tests/unit/test_compiler_vars.c` | Variable compile tests |

## Key Data Structures / API

No new public API — extends `MsCompiler` internal behavior.

## Implementation Notes

### Global Variables

```
var x = expr    // top-level (scope_depth == 0)
```
1. Compile `expr` → register `reg`
2. Emit `DEFGLOBAL reg, K(name_idx)` — `name_idx` is the string constant index for the variable name

Read global: `GETGLOBAL A Bx` — `R(A) = globals[K(Bx)]`
Write global: `SETGLOBAL A Bx` — `globals[K(Bx)] = R(A)`

### Local Variables

```
{ var x = expr }    // scope_depth > 0
```
1. `alloc_reg()` assigns a register slot to `x`
2. Compile `expr` into that register
3. Record in `locals[]`: `name=token`, `depth=scope_depth`, `slot=reg`, `is_captured=false`

**Identifier resolution**: scan `locals[]` backward for a name match → return `EDESC_LOCAL(slot)`. Not found → return `EDESC_GLOBAL(name_const_idx)`.

### Scope Management

```c
static void begin_scope(MsCompiler* c) { c->scope_depth++; }
static void end_scope(MsCompiler* c) {
    c->scope_depth--;
    while (c->local_count > 0 &&
           c->locals[c->local_count - 1].depth > c->scope_depth) {
        MsLocal* local = &c->locals[c->local_count - 1];
        if (local->is_captured) {
            emit(c, ms_enc_ABC(MS_OP_CLOSE, local->slot, 0, 0));
        }
        c->local_count--;
        free_reg(c, local->slot);
    }
}
```

### Assignment and Compound Assignment

Assignment is controlled by the `can_assign` parameter in the Pratt parser:
- When resolving an identifier, if `can_assign && match(EQUAL)` → compile RHS, emit `SET`
- `x += expr` is equivalent to `x = x + expr`: read `x` → compile `expr` → emit `ADD` → emit `SET`

### String Constant Deduplication

```c
static int add_string_constant(MsCompiler* c, MsObjString* s) {
    MsValue existing;
    if (ms_table_get(&c->string_cache, s, &existing)) {
        return (int)MS_AS_INT(existing);
    }
    int idx = ms_chunk_add_constant(current_chunk(c), MS_OBJ_VAL(s));
    ms_table_set(&c->string_cache, s, MS_INT_VAL(idx));
    return idx;
}
```

## C Unit Tests

```c
// tests/unit/test_compiler_vars.c
#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/vm.h"

static void test_global_var(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    MsObjFunction* fn = ms_compile(&vm, "var x = 10\nprint(x)", "<test>",
                                    diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "global_var");
    // Expect: LOADK R(0) K(10), DEFGLOBAL R(0) K("x"), GETGLOBAL R(0) K("print"), ...
    ms_vm_free(&vm);
}

static void test_local_scope(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    MsObjFunction* fn = ms_compile(&vm, "{ var a = 1\n var b = 2 }", "<test>",
                                    diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "local_scope");
    ms_vm_free(&vm);
}

int main(void) {
    test_global_var();
    test_local_scope();
    printf("test_compiler_vars: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/variables.ms (run after T13)
var x = 10
print(x)
// expect: 10
x = x + 1
print(x)
// expect: 11

var greeting = "hello"
print(greeting)
// expect: hello

{
  var inner = 42
  print(inner)
  // expect: 42
}

var a = 1
var b = 2
var c = a + b
print(c)
// expect: 3

// Compound assignment
var n = 10
n += 5
print(n)
// expect: 15
n -= 3
print(n)
// expect: 12
n *= 2
print(n)
// expect: 24
n /= 4
print(n)
// expect: 6
n %= 4
print(n)
// expect: 2

// Shadowing in nested scope
var s = "outer"
{
  var s = "inner"
  print(s)
  // expect: inner
}
print(s)
// expect: outer
```
