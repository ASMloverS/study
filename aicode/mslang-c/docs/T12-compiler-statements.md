# Task 12: Compiler — Statements and Control Flow

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Add all control-flow statements and function declarations to compiler: if/else, while, for, break/continue, switch/case, fun.
**Dependencies:** T11
**Produces:** Compiler supports complete control flow and function declarations

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/compiler.c` | Add if/while/for/switch/fun/return |
| Create | `tests/unit/test_compiler_stmts.c` | Control flow compile tests |

## Implementation Notes

### if / else

```
if (cond) { then } else { alt }
```
1. Compile `cond` → register
2. Emit `TEST A 0` + `JMP exit_then` (skip then if false)
3. Compile then-body
4. Emit `JMP exit_else` (skip else)
5. Patch `exit_then` target
6. Compile else-body
7. Patch `exit_else` target

### while

```
while (cond) { body }
```
1. `loop_start = current_ip`
2. Compile `cond` → `TEST + JMP(exit)`
3. Compile body
4. Emit `JMP(loop_start)`
5. Patch exit target

### for (C-style)

```
for (init; cond; post) { body }
```
1. `begin_scope`
2. Compile init (var declaration or expression)
3. `loop_start = current_ip`
4. Compile `cond` → `TEST + JMP(exit)`
5. Compile body (break/continue register to `LoopCtx`)
6. Compile post
7. `JMP(loop_start)`
8. Patch exit + break list
9. `end_scope`

### break / continue

- `break`: emit `JMP(placeholder)`, add to `loop->break_list` patch chain
- `continue`: emit `JMP(placeholder)`, jump to before the post expression (or `loop_start`)

The patch chain stores the previous offset in the `JMP` instruction's `sBx` field (in-place linked list):
```c
static void patch_list(MsCompiler* c, int list, int target) {
    while (list != NO_JUMP) {
        int next = get_jump_target(c, list);  // read old sBx
        patch_jump(c, list, target);
        list = next;
    }
}
```

### switch / case

```
switch (expr) { case v1: ... case v2: ... default: ... }
```
Compiled as a linear comparison chain:
1. Compile switch `expr` → register `reg`
2. For each case: compile case value → constant, `EQ reg K(value)`, `JMP(next_case)`, compile case body
3. `default`: compile body directly
4. Patch all case jumps

### Function Declarations

```
fun foo(a, b) { body }
```
1. Create nested `MsCompiler` (`enclosing` = current compiler)
2. Parameters become `locals[0]`, `locals[1]`, ...
3. Compile function body
4. End compilation → return `MsObjFunction*` (as proto)
5. Outer compiler emits `CLOSURE A Bx` (`Bx` = proto's constant pool index)

**return**: compile `expr` → `reg`, emit `RETURN reg 2` (1 value). No expression: `RETURN 0 1` (nil).

### Expression Statements and print

- `print(expr)` is handled as a builtin statement for now; it can also be compiled as a global function call
- Expression statement: compile expression, discard result (`free_reg`)

## C Unit Tests

```c
// tests/unit/test_compiler_stmts.c
#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/vm.h"

static void test_if_else(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "if (true) { print(1) } else { print(2) }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "if_else");
    ms_vm_free(&vm);
}

static void test_while_loop(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "var i = 0\nwhile (i < 3) { i = i + 1 }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "while");
    ms_vm_free(&vm);
}

static void test_function_decl(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "fun add(a, b) { return a + b }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "fun_decl");
    ms_vm_free(&vm);
}

int main(void) {
    test_if_else();
    test_while_loop();
    test_function_decl();
    printf("test_compiler_stmts: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/control_flow.ms (run after T13)
if (true) {
  print("yes")
}
// expect: yes

if (false) {
  print("no")
} else {
  print("else")
}
// expect: else

var i = 0
while (i < 3) {
  print(i)
  i = i + 1
}
// expect: 0
// expect: 1
// expect: 2

for (var j = 10; j < 13; j = j + 1) {
  print(j)
}
// expect: 10
// expect: 11
// expect: 12

fun add(a, b) {
  return a + b
}
print(add(3, 4))
// expect: 7

fun fib(n) {
  if (n <= 1) return n
  return fib(n - 1) + fib(n - 2)
}
print(fib(8))
// expect: 21

switch (2) {
  case 1: print("one")
  case 2: print("two")
  default: print("other")
}
// expect: two

var x = 15
if (x > 10) {
  if (x > 20) {
    print("very big")
  } else {
    print("medium")
  }
} else {
  print("small")
}
// expect: medium
```
