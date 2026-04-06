# Task 24: Exception Handling and Defer

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement try/catch/throw exception handling with stack unwinding, and Go-style defer statements.
**Dependencies:** T15, T18
**Produces:** try/catch 异常捕获, throw 抛出, defer 延迟执行

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `include/ms/vm.h` | 异常处理栈, CallFrame 添加 defer 缓冲 |
| Modify | `src/compiler.c` | try/catch/throw/defer 语句编译 |
| Modify | `src/vm.c` | TRY, ENDTRY, THROW, DEFER 操作码 |
| Modify | `src/vm_call.c` | 栈回退, defer 执行 |
| Create | `tests/unit/test_exceptions.c` | 异常和 defer 测试 |

## Key Data Structures / API

```c
// 异常处理器 (栈式)
typedef struct {
    MsInstruction* handler_ip;  // catch 块的 IP
    int frame_index;            // 所属帧索引
    int catch_reg;              // 捕获的异常值放入的寄存器
    MsValue* stack_top;         // 进入 try 时的栈顶 (用于恢复)
} MsExceptionHandler;

#define MS_MAX_EXCEPTION_HANDLERS 16

// 追加到 MsVM:
MsExceptionHandler exception_handlers[MS_MAX_EXCEPTION_HANDLERS];
int exception_count;

// 追加到 MsCallFrame:
MsObjClosure** deferred;   // defer 闭包缓冲区
int deferred_count;
int deferred_capacity;
```

## Implementation Notes

### 编译 try/catch

```ms
try {
  // body
} catch (e) {
  // handler
}
```

编译为:
```
TRY Bx(offset_to_catch)     ; 推入异常处理器
  ... body ...
ENDTRY                       ; 弹出异常处理器
JMP past_catch               ; 跳过 catch 块
catch_start:                  ; handler_ip 指向这里
  ... catch body ...          ; e 在 catch_reg 中
```

### TRY 操作码

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

### THROW 操作码

```c
case MS_OP_THROW: {
    MsValue error = R(A);
    if (!throw_exception(vm, error)) {
        // 无处理器: 未捕获异常 → runtime error
        ms_vm_runtime_error(vm, "Uncaught exception: %s", ...);
        return MS_INTERPRET_RUNTIME_ERROR;
    }
    // throw_exception 已更新 frame/ip
    frame = &vm->frames[vm->frame_count - 1];
    ip = frame->ip;
    break;
}
```

### 栈回退 (throw_exception)

```c
static bool throw_exception(MsVM* vm, MsValue error) {
    while (vm->exception_count > 0) {
        MsExceptionHandler* h = &vm->exception_handlers[vm->exception_count - 1];
        // 回退到 handler 所在的帧
        while (vm->frame_count - 1 > h->frame_index) {
            MsCallFrame* f = &vm->frames[--vm->frame_count];
            run_deferred(vm, f);  // 执行该帧的 defer
            close_upvalues(vm, f->slots);
        }
        vm->exception_count--;
        MsCallFrame* target = &vm->frames[h->frame_index];
        target->ip = h->handler_ip;
        target->slots[h->catch_reg] = error;  // 将异常值放入 catch 寄存器
        vm->stack_top = h->stack_top;
        return true;
    }
    return false;  // 无处理器
}
```

### DEFER 操作码

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

### Defer 执行 (LIFO)

函数返回 (RETURN) 或异常回退时, 按 LIFO 顺序执行 deferred 闭包:
```c
static void run_deferred(MsVM* vm, MsCallFrame* frame) {
    for (int i = frame->deferred_count - 1; i >= 0; i--) {
        // 调用 deferred[i] (0 参数)
        call_closure(vm, frame->deferred[i], 0);
        ms_vm_run(vm);  // 递归执行
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
