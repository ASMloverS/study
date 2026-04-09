# Task 29: Quickening (Adaptive Specialization)

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement runtime quickening: arithmetic opcodes adaptively specialize to type-specific variants; computed goto dispatch for GCC/Clang.
**Dependencies:** T13, T28
**Produces:** Arithmetic operations auto-specialize; dispatch performance improved

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/vm.c` | Quickened opcode handlers, computed goto |
| Modify | `include/ms/object.h` | ObjFunction.arith_deopt counter |
| Create | `tests/unit/test_quickening.c` | Quickening validation |

## Implementation Notes

### How Quickening Works

On first execution of `MS_OP_ADD`, check operand types:
- Both int → **rewrite** instruction in-place to `MS_OP_ADD_II`
- Both float → rewrite to `MS_OP_ADD_FF`
- Both string → rewrite to `MS_OP_ADD_SS`

Subsequent executions of the same instruction take the specialized path directly (no type check).

### Deoptimization

When a specialized variant encounters a type mismatch (e.g., ADD_II sees a float):
1. Increment the deopt counter for that instruction offset.
2. If `deopt_count < 3`: attempt re-specialization.
3. If `deopt_count >= 3`: fall back to generic `MS_OP_ADD` permanently.

```c
// In ObjFunction:
ms_u8* arith_deopt;   // lazy alloc, indexed by instruction offset
int arith_deopt_size;
```

### Specialization Dispatch Example

```c
case MS_OP_ADD: {
    MsValue b = RK(B), c = RK(C);
    if (MS_IS_INT(b) && MS_IS_INT(c)) {
        R(A) = MS_INT_VAL(MS_AS_INT(b) + MS_AS_INT(c));
        // Quicken: rewrite to ADD_II
        ip[-1] = ms_enc_ABC(MS_OP_ADD_II, A, B, C_val);
    } else if (MS_IS_NUMBER(b) && MS_IS_NUMBER(c)) {
        R(A) = MS_NUMBER_VAL(ms_as_double(b) + ms_as_double(c));
        ip[-1] = ms_enc_ABC(MS_OP_ADD_FF, A, B, C_val);
    } else if (MS_IS_STRING(b) && MS_IS_STRING(c)) {
        R(A) = MS_OBJ_VAL(ms_obj_string_concat(vm, ...));
        ip[-1] = ms_enc_ABC(MS_OP_ADD_SS, A, B, C_val);
    } else { /* mixed types */ }
    break;
}

case MS_OP_ADD_II: {
    MsValue b = RK(B), c = RK(C);
    if (MS_LIKELY(MS_IS_INT(b) && MS_IS_INT(c))) {
        R(A) = MS_INT_VAL(MS_AS_INT(b) + MS_AS_INT(c));
    } else {
        deopt_and_retry(vm, frame, &ip, MS_OP_ADD);
    }
    break;
}
```

### Computed Goto Dispatch

```c
#if defined(__GNUC__) || defined(__clang__)
  #define MS_USE_COMPUTED_GOTO 1
#endif

#ifdef MS_USE_COMPUTED_GOTO
  static void* dispatch_table[MS_OP_COUNT] = {
      [MS_OP_LOADK]    = &&op_loadk,
      [MS_OP_ADD]      = &&op_add,
      [MS_OP_ADD_II]   = &&op_add_ii,
      // ...
  };
  #define DISPATCH() goto *dispatch_table[MS_GET_OP(READ_INSTR())]
  #define CASE(op) op_##op:
#else
  #define DISPATCH() break
  #define CASE(op) case MS_OP_##op:
#endif
```

Usage:
```c
DISPATCH();  // initial dispatch
CASE(LOADK) {
    R(A) = K(MS_GET_Bx(instr));
    DISPATCH();
}
CASE(ADD) {
    // ...
    DISPATCH();
}
```

### Quickened Opcodes

| Generic | int-int | float-float | string-string |
|---------|---------|-------------|---------------|
| ADD | ADD_II | ADD_FF | ADD_SS |
| SUB | SUB_II | SUB_FF | — |
| MUL | MUL_II | MUL_FF | — |
| DIV | — | DIV_FF | — |
| LT | LT_II | LT_FF | — |
| EQ | EQ_II | — | — |

## C Unit Tests

```c
// tests/unit/test_quickening.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_add_quickens_to_ii(void) {
    MsVM vm; ms_vm_init(&vm);
    // Loop integer addition to trigger quickening
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var sum = 0\n"
        "for (var i = 0; i < 10; i = i + 1) { sum = sum + i }\n"
        "print(sum)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    // Can inspect function bytecode to verify ADD was replaced with ADD_II
    ms_vm_free(&vm);
}

static void test_deopt(void) {
    MsVM vm; ms_vm_init(&vm);
    // Integer add (quicken to II), then float (deopt + requicken to FF)
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun add(a, b) { return a + b }\n"
        "print(add(1, 2))\n"
        "print(add(1.5, 2.5))\n"
        "print(add(\"a\", \"b\"))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_add_quickens_to_ii();
    test_deopt();
    printf("test_quickening: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/quickening.ms
// Integer arithmetic (should quicken to _II variants)
var sum = 0
for (var i = 0; i < 100; i = i + 1) {
  sum = sum + i
}
print(sum)
// expect: 4950

// Float arithmetic (should quicken to _FF)
var fsum = 0.0
for (var i = 0; i < 10; i = i + 1) {
  fsum = fsum + 0.1
}
print(fsum > 0.9)
// expect: true

// String concat (should quicken to _SS)
var s = ""
for (var i = 0; i < 5; i = i + 1) {
  s = s + "x"
}
print(s)
// expect: xxxxx

// Deopt: same function called with different types
fun add(a, b) { return a + b }
print(add(1, 2))
// expect: 3
print(add(1.5, 2.5))
// expect: 4
print(add("hello", " world"))
// expect: hello world

// Comparison quickening
fun less(a, b) { return a < b }
print(less(1, 2))
// expect: true
print(less(2.0, 1.0))
// expect: false
```
