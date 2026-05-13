# Task 14: VM — Closures and Upvalues

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement CLOSURE opcode, upvalue capture (local/transitive), open upvalue chain, close semantics.
**Deps:** T13
**Produces:** Closures capture vars, upvalue closing, nested closures

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/vm.c` | CLOSURE, GETUPVAL, SETUPVAL, CLOSE |
| Modify | `src/vm_call.c` | Capture upvalues during closure creation |
| Create | `tests/unit/test_vm_closures.c` | Closure tests |

## Impl Notes

### CLOSURE Opcode

```c
case MS_OP_CLOSURE: {
    MsObjFunction* fn = MS_AS_FUNCTION(K(MS_GET_Bx(instr)));
    MsObjClosure* cl = ms_obj_closure_new(vm, fn);
    R(A) = MS_OBJ_VAL(cl);
    // Read trailing pseudo-instructions to populate the upvalue array
    for (int i = 0; i < fn->upvalue_count; i++) {
        MsInstruction uv_instr = READ_INSTR();
        bool is_local = MS_GET_A(uv_instr);
        int index = MS_GET_B(uv_instr);
        if (is_local) {
            cl->upvalues[i] = capture_upvalue(vm, &frame->slots[index]);
        } else {
            cl->upvalues[i] = frame->closure->upvalues[index];
        }
    }
    break;
}
```

### Open Upvalue Chain

`vm->open_upvalues`: linked list, descending stack order (highest addr first):
```c
static MsObjUpvalue* capture_upvalue(MsVM* vm, MsValue* local) {
    MsObjUpvalue* prev = NULL;
    MsObjUpvalue* uv = vm->open_upvalues;
    while (uv != NULL && uv->location > local) {
        prev = uv;
        uv = uv->next;
    }
    if (uv != NULL && uv->location == local) return uv;  // already captured
    MsObjUpvalue* created = ms_obj_upvalue_new(vm, local);
    created->next = uv;
    if (prev) prev->next = created; else vm->open_upvalues = created;
    return created;
}
```

### Close Upvalues

Scope exit / `CLOSE`: close open upvalues where `location >= last`:
```c
static void close_upvalues(MsVM* vm, MsValue* last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        MsObjUpvalue* uv = vm->open_upvalues;
        uv->closed = *uv->location;
        uv->location = &uv->closed;
        vm->open_upvalues = uv->next;
    }
}
```

### GET/SET UPVAL

```c
case MS_OP_GETUPVAL:
    R(A) = *frame->closure->upvalues[MS_GET_B(instr)]->location;
    break;
case MS_OP_SETUPVAL:
    *frame->closure->upvalues[MS_GET_B(instr)]->location = R(A);
    break;
```

## C Unit Tests

```c
// tests/unit/test_vm_closures.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_closure_captures(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun make() { var x = 10\n return fun() { return x } }\n"
        "print(make()())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_closure_captures();
    printf("test_vm_closures: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/closures.ms
fun make_counter() {
  var count = 0
  fun increment() {
    count = count + 1
    return count
  }
  return increment
}
var counter = make_counter()
print(counter())
// expect: 1
print(counter())
// expect: 2
print(counter())
// expect: 3

// tests/fixtures/closure_adder.ms
fun make_adder(n) {
  return fun(x) { return x + n }
}
var add5 = make_adder(5)
var add10 = make_adder(10)
print(add5(3))
// expect: 8
print(add10(3))
// expect: 13

// tests/fixtures/nested_closures.ms
fun outer() {
  var x = 1
  fun middle() {
    var y = 2
    fun inner() {
      return x + y
    }
    return inner
  }
  return middle
}
print(outer()()())
// expect: 3

// tests/fixtures/closure_in_loop.ms (requires T21 — lists)
// Note: requires T21 (lists) to run; kept for reference

// tests/fixtures/upvalue_close.ms
var val = 0
fun setter(v) { val = v }
fun getter() { return val }
setter(42)
print(getter())
// expect: 42
```
