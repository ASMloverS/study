# Task 23: String Interpolation Execution

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Complete string interpolation end-to-end: compile `"a ${expr} b"` into concatenation bytecode, execute at runtime.
**Dependencies:** T09, T13, T22
**Produces:** `"hello ${name}!"` correctly concatenated at runtime

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/compiler_expr.c` | Compile string interpolation token sequence |
| Modify | `src/vm.c` | Ensure STR (tostring) + ADD (concat) work correctly |
| Create | `tests/unit/test_interp_exec.c` | Interpolation execution tests |

## Implementation Notes

### Compilation Strategy

Token sequence from scanner (T09):
```
"hello "     → STRING_INTERP
name         → IDENTIFIER
"!"          → STRING_INTERP_END
```

Compiler translates this into concatenation operations:
```
LOADK   R(0) K("hello ")
GETLOCAL R(1) slot(name)   ; or other expr
STR     R(1) R(1)          ; tostring (if not already a string)
ADD     R(0) R(0) R(1)     ; concat
LOADK   R(1) K("!")
ADD     R(0) R(0) R(1)     ; concat
```

Steps:
1. On `STRING_INTERP` token → load prefix text as string constant
2. Compile interpolated expression → register `reg`
3. Emit `STR dest, reg` (convert value to string)
4. Emit `ADD dest, prefix_reg, str_reg` (concat)
5. If next is `STRING_INTERP_END` → load suffix, concat again
6. If next is another `STRING_INTERP` → repeat steps 1–4

### MS_OP_STR Implementation

```c
case MS_OP_STR: {
    MsValue val = R(MS_GET_B(instr));
    if (MS_IS_STRING(val)) {
        R(A) = val;  // already a string
    } else {
        char* s = ms_value_to_cstring(val);
        R(A) = MS_OBJ_VAL(ms_obj_string_take(vm, s, (int)strlen(s)));
    }
    break;
}
```

### Optimization Notes

Multi-segment interpolation `"a ${x} b ${y} c"` can use StringBuilder to avoid many intermediate strings:
- Simple: multiple ADD ops (sufficient for initial version)
- Advanced: compile to NEWSTR_BUILDER → APPEND × N → TO_STRING (deferred to T28+)

## C Unit Tests

```c
// tests/unit/test_interp_exec.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_simple_interp(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var name = \"world\"\nprint(\"hello ${name}!\")", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_expr_interp(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "print(\"${1 + 2} items\")", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_simple_interp();
    test_expr_interp();
    printf("test_interp_exec: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/string_interp_runtime.ms
var name = "world"
print("hello ${name}!")
// expect: hello world!

print("${1 + 2} items")
// expect: 3 items

var x = 42
print("x = ${x}")
// expect: x = 42

print("${true} or ${false}")
// expect: true or false

print("nested: ${"inner ${1}"}")
// expect: nested: inner 1

print("${100}")
// expect: 100

var a = "A"
var b = "B"
print("${a} and ${b}")
// expect: A and B

print("len = ${"hello".len()}")
// expect: len = 5

var n = nil
print("value is ${n}")
// expect: value is nil
```
