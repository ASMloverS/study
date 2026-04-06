# Task 25: Coroutines and Generators

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement generator functions (`fun*`), ObjCoroutine with independent stacks, yield/resume semantics.
**Dependencies:** T14, T24
**Produces:** `fun* gen() { yield val }` 生成器, `resume(co)` 恢复执行

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `include/ms/object.h` | ObjCoroutine 结构 |
| Modify | `src/object.c` | 协程创建/销毁/打印 |
| Modify | `src/compiler.c` | `fun*` 语法, yield 语句 |
| Modify | `src/vm.c` | YIELD, RESUME 操作码 |
| Modify | `src/vm_call.c` | 协程调度 (栈切换) |
| Create | `tests/unit/test_coroutines.c` | 协程测试 |

## Key Data Structures / API

```c
typedef enum {
    MS_CORO_CREATED,    // 已创建但未启动
    MS_CORO_RUNNING,    // 正在执行
    MS_CORO_SUSPENDED,  // yield 后暂停
    MS_CORO_DEAD,       // 执行完毕
} MsCoroState;

typedef struct {
    MsObject obj;
    MsCoroState state;
    MsObjClosure* closure;
    // 独立的栈和帧
    MsValue* stack;
    int stack_size;
    MsValue* stack_top;
    MsCallFrame* frames;
    int frame_count;
    int frame_capacity;
    // Open upvalues (独立链表)
    MsObjUpvalue* open_upvalues;
} MsObjCoroutine;

#define MS_IS_COROUTINE(v)  MS_IS_OBJ_TYPE(v, MS_OBJ_COROUTINE)
#define MS_AS_COROUTINE(v)  ((MsObjCoroutine*)MS_AS_OBJECT(v))

MsObjCoroutine* ms_obj_coroutine_new(MsVM* vm, MsObjClosure* cl);
```

## Implementation Notes

### 编译 `fun*`

```ms
fun* range(n) {
  for (var i = 0; i < n; i = i + 1) {
    yield i
  }
}
```

编译器设 `function->is_generator = true`. yield 编译为 `MS_OP_YIELD A B` (B-1 个值从 R(A)).

### 调用生成器函数

当 CALL 遇到 generator closure: 不立即执行, 而是创建 ObjCoroutine 并返回:
```c
if (closure->function->is_generator) {
    MsObjCoroutine* co = ms_obj_coroutine_new(vm, closure);
    // 设置协程的初始帧 (closure + 参数拷贝到协程栈)
    R(A) = MS_OBJ_VAL(co);
    // 不进入协程执行
}
```

### RESUME 操作码

`RESUME A B C` — resume coroutine R(B), 传入 R(C) 作为 yield 返回值:
```c
case MS_OP_RESUME: {
    MsObjCoroutine* co = MS_AS_COROUTINE(R(MS_GET_B(instr)));
    MsValue sent = RK(MS_GET_C(instr));
    if (co->state == MS_CORO_DEAD) {
        runtime_error("Cannot resume dead coroutine");
        break;
    }
    // 保存当前 VM 状态
    save_vm_state(vm);
    // 切换到协程栈
    swap_to_coroutine(vm, co, sent);
    co->state = MS_CORO_RUNNING;
    // 继续执行
    MsInterpretResult r = ms_vm_run(vm);
    // 执行到 YIELD 或 RETURN 后回来
    restore_vm_state(vm);
    R(A) = co->yield_value;
    break;
}
```

### YIELD 操作码

```c
case MS_OP_YIELD: {
    MsValue val = R(A);
    // 保存协程状态
    save_coroutine_state(vm, co);
    co->state = MS_CORO_SUSPENDED;
    co->yield_value = val;
    // 返回到 caller (ms_vm_run returns)
    return MS_INTERPRET_OK;
}
```

### 栈切换 (O(1) 指针交换)

```c
static void swap_to_coroutine(MsVM* vm, MsObjCoroutine* co, MsValue sent) {
    // 保存 caller 状态到某处 (可用 vm 的额外字段或调用者的协程)
    vm->saved_stack = vm->stack;  // 简化: 用独立栈数组
    // 切换到协程栈
    vm->stack_top = co->stack_top;
    // 将 sent 值放入协程的 yield 返回位置
}
```

简化实现: 使用递归 `ms_vm_run` 而非真正的栈切换. 协程有自己的 stack/frames 数组, swap 时交换 vm 的指针.

### 默认参数和 Rest 参数

同时在此任务实现 (编译器 + VM):
- 默认参数: `fun foo(a, b = 10)` → `min_arity = 1, arity = 2`, 缺失参数用默认值填充
- Rest 参数: `fun foo(a, ...rest)` → 将多余参数打包为 ObjList

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
