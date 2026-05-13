# Task 24: Exception Handling and Defer

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** try/catch/throw exception handling with stack unwinding + Go-style defer.
**Deps:** T15, T18
**Produces:** try/catch exception handling, throw, defer deferred execution

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `include/ms/vm.h` | Exception handler stack, defer buffer in CallFrame |
| Modify | `src/compiler.c` | try/catch/throw/defer compilation |
| Modify | `src/vm.c` | TRY, ENDTRY, THROW, DEFER opcodes |
| Modify | `src/vm_call.c` | Stack unwinding, defer execution |
| Create | `tests/unit/test_exceptions.c` | Exception and defer tests |

## Key Data Structures / API

```c
// Exception handler (stack-based)
typedef struct {
    MsInstruction* handler_ip;  // IP of catch block
    int frame_index;            // index of owning frame
    int catch_reg;              // register for caught exception value
    MsValue* stack_top;         // stack top at try entry (for restoration)
} MsExceptionHandler;

#define MS_MAX_EXCEPTION_HANDLERS 16

// Append to MsVM:
MsExceptionHandler exception_handlers[MS_MAX_EXCEPTION_HANDLERS];
int exception_count;

// Append to MsCallFrame:
MsObjClosure** deferred;   // defer closure buffer
int deferred_count;
int deferred_capacity;
```

## Impl Notes

### Compiling try/catch

```ms
try {
  // body
} catch (e) {
  // handler
}
```

Compiles to:
```
TRY Bx(offset_to_catch)     ; push exception handler
  ... body ...
ENDTRY                       ; pop exception handler
JMP past_catch               ; skip catch block
catch_start:                  ; handler_ip points here
  ... catch body ...          ; e is in catch_reg
```

### TRY Opcode

```c
case MS_OP_TRY: {
    int offset = MS_GET_Bx(instr);
    MsExceptionHandler* h = &vm->exception_handlers[vm->exception_count++];
    h->handler_ip = ip + offset;
    h->frame_index = vm->frame_count - 1;
    h->catch_reg = A;
    h->stack_top = vm->stack_top;
    break;
}
case MS_OP_ENDTRY: {
    vm->exception_count--;
    break;
}
```

### THROW Opcode

```c
case MS_OP_THROW: {
    MsValue error = R(A);
    if (!throw_exception(vm, error)) {
        // No handler: uncaught exception → runtime error
        ms_vm_runtime_error(vm, "Uncaught exception: %s", ...);
        return MS_INTERPRET_RUNTIME_ERROR;
    }
    // throw_exception has updated frame/ip
    frame = &vm->frames[vm->frame_count - 1];
    ip = frame->ip;
    break;
}
```

### Stack Unwinding (throw_exception)

```c
static bool throw_exception(MsVM* vm, MsValue error) {
    while (vm->exception_count > 0) {
        MsExceptionHandler* h = &vm->exception_handlers[vm->exception_count - 1];
        // Unwind to the frame containing the handler
        while (vm->frame_count - 1 > h->frame_index) {
            MsCallFrame* f = &vm->frames[--vm->frame_count];
            run_deferred(vm, f);  // run deferred for this frame
            close_upvalues(vm, f->slots);
        }
        vm->exception_count--;
        MsCallFrame* target = &vm->frames[h->frame_index];
        target->ip = h->handler_ip;
        target->slots[h->catch_reg] = error;  // place exception in catch register
        vm->stack_top = h->stack_top;
        return true;
    }
    return false;  // no handler
}
```

### DEFER Opcode

```c
case MS_OP_DEFER: {
    MsObjClosure* cl = MS_AS_CLOSURE(R(A));
    MsCallFrame* f = frame;
    if (f->deferred_count >= f->deferred_capacity) {
        int cap = f->deferred_capacity < 4 ? 4 : f->deferred_capacity * 2;
        f->deferred = realloc(f->deferred, sizeof(MsObjClosure*) * cap);
        f->deferred_capacity = cap;
    }
    f->deferred[f->deferred_count++] = cl;
    break;
}
```

### Defer Execution (LIFO)

On `RETURN` or exception unwind: execute deferred closures LIFO:
```c
static void run_deferred(MsVM* vm, MsCallFrame* frame) {
    for (int i = frame->deferred_count - 1; i >= 0; i--) {
        // call deferred[i] (0 args)
        call_closure(vm, frame->deferred[i], 0);
        ms_vm_run(vm);  // recursive execution
    }
    frame->deferred_count = 0;
}
```

## C Unit Tests

```c
// tests/unit/test_exceptions.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_try_catch(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "try { throw \"oops\" } catch (e) { print(e) }", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_try_catch();
    printf("test_exceptions: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/try_catch.ms
try {
  throw "error!"
} catch (e) {
  print(e)
}
// expect: error!

// tests/fixtures/try_catch_nested.ms
try {
  try {
    throw "inner"
  } catch (e) {
    print("caught: " + e)
    throw "rethrown"
  }
} catch (e2) {
  print("outer: " + e2)
}
// expect: caught: inner
// expect: outer: rethrown

// tests/fixtures/try_no_throw.ms
try {
  print("ok")
} catch (e) {
  print("error")
}
// expect: ok

// tests/fixtures/throw_across_frames.ms
fun dangerous() {
  throw "boom"
}
try {
  dangerous()
} catch (e) {
  print(e)
}
// expect: boom

// tests/fixtures/defer_basic.ms
fun test() {
  defer fun() { print("deferred") }
  print("first")
}
test()
// expect: first
// expect: deferred

// tests/fixtures/defer_order.ms
fun test() {
  defer fun() { print("A") }
  defer fun() { print("B") }
  defer fun() { print("C") }
  print("body")
}
test()
// expect: body
// expect: C
// expect: B
// expect: A

// tests/fixtures/defer_with_return.ms
fun test() {
  defer fun() { print("cleanup") }
  return 42
}
print(test())
// expect: cleanup
// expect: 42

// tests/fixtures/defer_with_throw.ms
fun risky() {
  defer fun() { print("defer ran") }
  throw "fail"
}
try {
  risky()
} catch (e) {
  print("caught: " + e)
}
// expect: defer ran
// expect: caught: fail
```
