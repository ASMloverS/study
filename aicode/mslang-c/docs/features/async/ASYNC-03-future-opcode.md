# ASYNC-03: ObjFuture + OP_AWAIT

## ObjFuture

### 对象定义（`include/ms/object.h`）

```c
typedef enum {
    MS_FUTURE_PENDING,
    MS_FUTURE_RESOLVED,
    MS_FUTURE_REJECTED,
} MsFutureState;

typedef struct MsWaiter {
    MsObjCoroutine* coro;    // 等待此 future 的协程
    int result_reg;          // future 完成后结果写入的寄存器
    struct MsWaiter* next;
} MsWaiter;

typedef struct {
    MsObject obj;            // GC header，type = MS_OBJ_FUTURE
    MsFutureState state;
    MsValue value;           // RESOLVED 时的结果，REJECTED 时的错误
    MsWaiter* waiters;       // 链表，所有 await 此 future 的协程
} MsObjFuture;
```

### 实现（`src/object.c`）

```c
MsObjFuture* ms_obj_future_new(MsVM* vm) {
    MsObjFuture* f = (MsObjFuture*)ms_allocate_object(vm, sizeof(MsObjFuture), MS_OBJ_FUTURE);
    f->state   = MS_FUTURE_PENDING;
    f->value   = MS_NIL_VAL();
    f->waiters = NULL;
    return f;
}

// async fun 调用时创建 future，内部持有一个 Coroutine
MsObjFuture* ms_obj_future_from_async(MsVM* vm, MsObjClosure* closure, int argc, MsValue* argv) {
    MsObjFuture* fut = ms_obj_future_new(vm);
    // 创建协程，但不立即 resume
    MsObjCoroutine* coro = ms_obj_coroutine_new(vm, closure);
    // 将协程包入 future（future.value 暂存 coro，resolved 后替换为返回值）
    fut->value = MS_OBJECT_VAL((MsObject*)coro);
    // argv 推入协程初始栈（参数）
    for (int i = 0; i < argc; i++) {
        coro->ctx.stack_top[i] = argv[i];
    }
    coro->ctx.stack_top += argc;
    return fut;
}
```

### GC（`src/vm_gc.c`）

```c
case MS_OBJ_FUTURE: {
    MsObjFuture* f = (MsObjFuture*)obj;
    ms_mark_value(vm, f->value);
    for (MsWaiter* w = f->waiters; w; w = w->next) {
        ms_mark_object(vm, (MsObject*)w->coro);
    }
    break;
}
```

### resolve / reject API（`include/ms/object.h` inline）

```c
void ms_future_resolve(MsVM* vm, MsObjFuture* fut, MsValue result);
void ms_future_reject(MsVM* vm, MsObjFuture* fut, MsValue error);
```

`ms_future_resolve` 实现：

```c
void ms_future_resolve(MsVM* vm, MsObjFuture* fut, MsValue result) {
    assert(fut->state == MS_FUTURE_PENDING);
    fut->state = MS_FUTURE_RESOLVED;
    fut->value = result;
    // 把所有 waiters 推回 EventLoop 就绪队列
    for (MsWaiter* w = fut->waiters; w; w = w->next) {
        w->coro->ctx.frames[...].slots[w->result_reg] = result;
        ms_loop_call_soon(vm->event_loop, w->coro);
    }
    fut->waiters = NULL;
}
```

## OP_AWAIT（`include/ms/opcode.h` + `src/vm.c`）

### 新增 opcode

```c
MS_OP_AWAIT,   // A=result_reg, B=future_reg
```

格式：iABC，C 未使用。

### VM 处理（`src/vm.c` dispatch loop）

```c
CASE(MS_OP_AWAIT) {
    MsValue fval = frame->slots[B];
    if (!MS_IS_OBJECT(fval) || MS_OBJ_TYPE(fval) != MS_OBJ_FUTURE) {
        runtime_error(vm, "await: expected Future, got %s", ms_type_name(fval));
        return MS_INTERPRET_RUNTIME_ERROR;
    }
    MsObjFuture* fut = (MsObjFuture*)MS_AS_OBJECT(fval);

    if (fut->state == MS_FUTURE_RESOLVED) {
        frame->slots[A] = fut->value;
        DISPATCH();
    }
    if (fut->state == MS_FUTURE_REJECTED) {
        // 抛出异常（走现有 exception 路径 T24）
        vm_throw(vm, fut->value);
        return MS_INTERPRET_RUNTIME_ERROR;
    }
    // PENDING：挂起当前协程，注册 waiter
    MsObjCoroutine* coro = vm->current_coroutine;
    if (!coro) {
        runtime_error(vm, "await outside async context");
        return MS_INTERPRET_RUNTIME_ERROR;
    }
    MsWaiter* w = ms_allocate(vm, sizeof(MsWaiter));
    w->coro       = coro;
    w->result_reg = A;
    w->next       = fut->waiters;
    fut->waiters  = w;
    coro->state   = MS_CORO_SUSPENDED;
    return MS_INTERPRET_YIELD;  // 控制权交还 EventLoop
}
```

## gather() 内建原生函数

```ms
var results = await gather([fut1, fut2, fut3])
```

`gather` 创建一个父 future，当所有子 future resolve 后 resolve 父 future：

```c
// src/vm_natives.c
static MsValue native_gather(MsVM* vm, int argc, MsValue* argv) {
    // argv[0] = ObjList of futures
    MsObjList* list = MS_AS_LIST(argv[0]);
    int n = list->items.count;
    MsObjFuture* all_fut = ms_obj_future_new(vm);
    // 分配计数器（可用 ObjList 的 count 字段 + 结果数组）
    // 为每个子 future 注册回调：子完成时填 results[i]，全部完成则 resolve all_fut
    for (int i = 0; i < n; i++) {
        MsObjFuture* f = (MsObjFuture*)MS_AS_OBJECT(list->items.data[i]);
        ms_future_add_gather_waiter(vm, f, all_fut, i, n);
    }
    return MS_OBJECT_VAL((MsObject*)all_fut);
}
```

## 验证

- `tests/unit/test_future.c`：
  - pending future + resolve → value 可取
  - reject → 异常传播
  - 多 waiter 注册 + resolve 后全部唤醒
- `tests/fixtures/async_future_manual.ms`：手动 resolve future 验证 await 语义
