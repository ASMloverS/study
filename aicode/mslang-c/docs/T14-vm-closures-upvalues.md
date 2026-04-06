# Task 14: VM — Closures and Upvalues

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement CLOSURE opcode, upvalue capture (local/transitive), open upvalue chain, and close semantics.
**Dependencies:** T13
**Produces:** 闭包捕获变量, 上值关闭, 嵌套闭包

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/vm.c` | CLOSURE, GETUPVAL, SETUPVAL, CLOSE |
| Modify | `src/vm_call.c` | 闭包创建时捕获上值 |
| Create | `tests/unit/test_vm_closures.c` | 闭包测试 |

## Implementation Notes

### CLOSURE 指令

```c
case MS_OP_CLOSURE: {
    MsObjFunction* fn = MS_AS_FUNCTION(K(MS_GET_Bx(instr)));
    MsObjClosure* cl = ms_obj_closure_new(vm, fn);
    R(A) = MS_OBJ_VAL(cl);
    // 读取后续伪指令来填充 upvalue 数组
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

`vm->open_upvalues` 按栈位置降序排列的链表 (栈顶在前):
```c
static MsObjUpvalue* capture_upvalue(MsVM* vm, MsValue* local) {
    MsObjUpvalue* prev = NULL;
    MsObjUpvalue* uv = vm->open_upvalues;
    while (uv != NULL && uv->location > local) {
        prev = uv;
        uv = uv->next;
    }
    if (uv != NULL && uv->location == local) return uv;  // 已存在
    MsObjUpvalue* created = ms_obj_upvalue_new(vm, local);
    created->next = uv;
    if (prev) prev->next = created; else vm->open_upvalues = created;
    return created;
}
```

### Close Upvalues

作用域退出或 CLOSE 指令时, 关闭所有 location >= last 的 open upvalue:
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

// tests/fixtures/closure_in_loop.ms
var fns = []
for (var i = 0; i < 3; i = i + 1) {
  var val = i
  fns.push(fun() { return val })
}
// 注: 此测试需要 T21 (列表) 才能运行; 先留作参考

// tests/fixtures/upvalue_close.ms
fun make_pair() {
  var x = 0
  fun getter() { return x }
  fun setter(v) { x = v }
  return [getter, setter]
}
// 注: 需要 T21 (列表) — 可改为嵌套调用版本:
var val = 0
fun setter(v) { val = v }
fun getter() { return val }
setter(42)
print(getter())
// expect: 42
```
