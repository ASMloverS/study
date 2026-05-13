# Task 25: Coroutines and Generators

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Generator functions (`fun*`), ObjCoroutine with independent stacks, yield/resume semantics.
**Deps:** T14, T24
**Produces:** `fun* gen() { yield val }` generators, `resume(co)` resumes execution

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `include/ms/object.h` | ObjCoroutine struct |
| Modify | `src/object.c` | Coroutine create/destroy/print |
| Modify | `src/compiler.c` | `fun*` syntax, yield statement |
| Modify | `src/vm.c` | YIELD, RESUME opcodes |
| Modify | `src/vm_call.c` | Coroutine scheduling (stack swap) |
| Create | `tests/unit/test_coroutines.c` | Coroutine tests |

## Key Data Structures / API

```c
typedef enum {
    MS_CORO_CREATED,    // created but not yet started
    MS_CORO_RUNNING,    // currently running
    MS_CORO_SUSPENDED,  // suspended after yield
    MS_CORO_DEAD,       // execution complete
} MsCoroState;

typedef struct {
    MsObject obj;
    MsCoroState state;
    MsObjClosure* closure;
    // Independent stack and frames
    MsValue* stack;
    int stack_size;
    MsValue* stack_top;
    MsCallFrame* frames;
    int frame_count;
    int frame_capacity;
    // Open upvalues (independent chain)
    MsObjUpvalue* open_upvalues;
} MsObjCoroutine;

#define MS_IS_COROUTINE(v)  MS_IS_OBJ_TYPE(v, MS_OBJ_COROUTINE)
#define MS_AS_COROUTINE(v)  ((MsObjCoroutine*)MS_AS_OBJECT(v))

MsObjCoroutine* ms_obj_coroutine_new(MsVM* vm, MsObjClosure* cl);
```

## Impl Notes

### Compiling `fun*`

```ms
fun* range(n) {
  for (var i = 0; i < n; i = i + 1) {
    yield i
  }
}
```

Compiler sets `function->is_generator = true`. `yield` → `MS_OP_YIELD A B` (B-1 values from `R(A)`).

### Calling a Generator Function

`CALL` + generator closure → create `ObjCoroutine`, return immediately:
```c
if (closure->function->is_generator) {
    MsObjCoroutine* co = ms_obj_coroutine_new(vm, closure);
    // set up coroutine's initial frame (closure + args copied to coroutine stack)
    R(A) = MS_OBJ_VAL(co);
    // do not enter the coroutine
}
```

### RESUME Opcode

`RESUME A B C` — resume coroutine `R(B)`, passing `R(C)` as yield return value:
```c
case MS_OP_RESUME: {
    MsObjCoroutine* co = MS_AS_COROUTINE(R(MS_GET_B(instr)));
    MsValue sent = RK(MS_GET_C(instr));
    if (co->state == MS_CORO_DEAD) {
        runtime_error("Cannot resume dead coroutine");
        break;
    }
    // Save current VM state
    save_vm_state(vm);
    // Switch to coroutine stack
    swap_to_coroutine(vm, co, sent);
    co->state = MS_CORO_RUNNING;
    // Continue execution
    MsInterpretResult r = ms_vm_run(vm);
    // Returns after YIELD or RETURN
    restore_vm_state(vm);
    R(A) = co->yield_value;
    break;
}
```

### YIELD Opcode

```c
case MS_OP_YIELD: {
    MsValue val = R(A);
    // Save coroutine state
    save_coroutine_state(vm, co);
    co->state = MS_CORO_SUSPENDED;
    co->yield_value = val;
    // Return to caller (ms_vm_run returns)
    return MS_INTERPRET_OK;
}
```

### Stack Swap (O(1) pointer exchange)

```c
static void swap_to_coroutine(MsVM* vm, MsObjCoroutine* co, MsValue sent) {
    // Save caller state (use extra VM fields or caller's coroutine)
    vm->saved_stack = vm->stack;  // simplification: use independent stack arrays
    // Switch to coroutine stack
    vm->stack_top = co->stack_top;
    // Place sent value at coroutine's yield return slot
}
```

Simplified: recursive `ms_vm_run` rather than true stack switching. Coroutine has own `stack`/`frames`; swap by exchanging VM pointers.

### Default and Rest Parameters (compiler + VM)

- Default params: `fun foo(a, b = 10)` → `min_arity = 1, arity = 2`; missing args filled with defaults
- Rest params: `fun foo(a, ...rest)` → packs extra args into `ObjList`

## C Unit Tests

```c
// tests/unit/test_coroutines.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_generator_basic(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun* count() { yield 1\n yield 2\n yield 3 }\n"
        "var g = count()\n"
        "print(resume(g))\nprint(resume(g))\nprint(resume(g))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_generator_basic();
    printf("test_coroutines: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/generator_basic.ms
fun* count_to(n) {
  for (var i = 1; i <= n; i = i + 1) {
    yield i
  }
}
var g = count_to(3)
print(resume(g))
// expect: 1
print(resume(g))
// expect: 2
print(resume(g))
// expect: 3

// tests/fixtures/generator_infinite.ms
fun* naturals() {
  var n = 0
  while (true) {
    yield n
    n = n + 1
  }
}
var g = naturals()
print(resume(g))
// expect: 0
print(resume(g))
// expect: 1
print(resume(g))
// expect: 2

// tests/fixtures/generator_send.ms
fun* accumulator() {
  var total = 0
  while (true) {
    var n = yield total
    total = total + n
  }
}
var acc = accumulator()
resume(acc)
print(resume(acc, 10))
// expect: 10
print(resume(acc, 20))
// expect: 30
print(resume(acc, 5))
// expect: 35

// tests/fixtures/default_params.ms
fun greet(name, greeting = "Hello") {
  return greeting + ", " + name + "!"
}
print(greet("World"))
// expect: Hello, World!
print(greet("World", "Hi"))
// expect: Hi, World!

// tests/fixtures/rest_params.ms
fun sum(...args) {
  var total = 0
  for (var i = 0; i < args.len(); i = i + 1) {
    total = total + args[i]
  }
  return total
}
print(sum(1, 2, 3))
// expect: 6
print(sum(10, 20))
// expect: 30
```
