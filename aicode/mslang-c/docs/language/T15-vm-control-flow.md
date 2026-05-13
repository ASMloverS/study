# Task 15: VM — Break, Continue, Switch, Lambda

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Complete VM support for break/continue in loops, switch/case dispatch, anonymous functions (lambda).
**Deps:** T14
**Produces:** break/continue in loops, switch dispatch, anonymous functions

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/vm.c` | Ensure JMP/TEST correctly support loop control |
| Modify | `src/compiler.c` | Complete break/continue patching, lambda syntax |
| Create | `tests/unit/test_vm_control.c` | Control flow tests |

## Impl Notes

### break / continue

Handled compiler-side (T12): emit `JMP` into `LoopCtx.break_list`. VM executes `JMP` (T13). This task ensures:
- `break` in nested loops exits only innermost loop
- `continue` → loop's post expr (`for`) or condition (`while`)
- `break` in `switch` exits switch without affecting outer loops

### Lambda (Anonymous Functions)

Syntax: `fun(x) { return x * 2 }` or `fun(x, y) { return x + y }`

Same as named fn; `ObjFunction.name = NULL` / `<lambda>`. Created via `CLOSURE`.

### switch Execution

Linear EQ+JMP chain (compiler-side). VM executes EQ comparisons + conditional jumps only.

## C Unit Tests

```c
// tests/unit/test_vm_control.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_break(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var i = 0\n"
        "while (true) {\n"
        "  if (i >= 3) break\n"
        "  print(i)\n"
        "  i = i + 1\n"
        "}", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_break();
    printf("test_vm_control: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/break.ms
var i = 0
while (true) {
  if (i >= 3) break
  print(i)
  i = i + 1
}
// expect: 0
// expect: 1
// expect: 2

// tests/fixtures/continue.ms
for (var i = 0; i < 5; i = i + 1) {
  if (i == 2) continue
  if (i == 4) continue
  print(i)
}
// expect: 0
// expect: 1
// expect: 3

// tests/fixtures/nested_break.ms
for (var i = 0; i < 3; i = i + 1) {
  for (var j = 0; j < 3; j = j + 1) {
    if (j == 1) break
    print(i * 10 + j)
  }
}
// expect: 0
// expect: 10
// expect: 20

// tests/fixtures/switch.ms
fun classify(n) {
  switch (n) {
    case 1: return "one"
    case 2: return "two"
    case 3: return "three"
    default: return "other"
  }
}
print(classify(1))
// expect: one
print(classify(2))
// expect: two
print(classify(5))
// expect: other

// tests/fixtures/lambda.ms
var double_it = fun(x) { return x * 2 }
print(double_it(5))
// expect: 10

fun apply(f, x) {
  return f(x)
}
print(apply(fun(n) { return n + 100 }, 42))
// expect: 142

// tests/fixtures/higher_order.ms
fun make_multiplier(factor) {
  return fun(x) { return x * factor }
}
var triple = make_multiplier(3)
print(triple(7))
// expect: 21
```
