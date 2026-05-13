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
    int frame_index;         // await 时记录的帧索引（resume 后稳定回写寄存器）
    int result_reg;          // future 完成后结果写入的寄存器
    struct MsWaiter* next;
} MsWaiter;

typedef struct {
    MsObject obj;            // GC header，type = MS_OBJ_FUTURE
    MsFutureState state;
    MsObjCoroutine* coro;    // PENDING 时持有协程；完成后置 NULL
    MsValue result;          // RESOLVED→返回值；REJECTED→错误值
    MsWaiter* waiters;       // 链表，所有 await 此 future 的协程
} MsObjFuture;
```

### 实现（`src/object.c`）

```c
MsObjFuture* ms_obj_future_new(MsVM* vm) {
    MsObjFuture* f = (MsObjFuture*)ms_allocate_object(vm, sizeof(MsObjFuture), MS_OBJ_FUTURE);
    f->state   = MS_FUTURE_PENDING;
    f->coro    = NULL;
    f->result  = MS_NIL_VAL();
    f->waiters = NULL;
    return f;
}

// async fun 调用时创建 future，内部持有一个 Coroutine
MsObjFuture* ms_obj_future_from_async(MsVM* vm, MsObjClosure* closure, int argc, MsValue* argv) {
    MsObjFuture* fut = ms_obj_future_new(vm);
    MsObjCoroutine* coro = ms_obj_coroutine_new(vm, closure);
    // 通过统一 call 路径装载参数（处理 arity / defaults / rest），
    // 同时在 coro->return_future 记录双向引用（由 vm_call.c 负责）
    ms_vm_call_closure_into_coro(vm, coro, closure, argc, argv);
    fut->coro = coro;
    return fut;
}
```

### GC（`src/vm_gc.c`）

```c
case MS_OBJ_FUTURE: {
    MsObjFuture* f = (MsObjFuture*)obj;
    if (f->coro) ms_mark_object(vm, (MsObject*)f->coro);
    ms_mark_value(vm, f->result);
    for (MsWaiter* w = f->waiters; w; w = w->next)
        ms_mark_object(vm, (MsObject*)w->coro);
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
    fut->state  = MS_FUTURE_RESOLVED;
    fut->coro   = NULL;       // 释放对协程的持有
    fut->result = result;
    for (MsWaiter* w = fut->waiters; w; w = w->next) {
        // frame_index 在 await 时记录，resume 后帧索引不变，安全回写
        w->coro->ctx.frames[w->frame_index].slots[w->result_reg] = result;
        ms_loop_call_soon(&vm->event_loop, w->coro);
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

### 返回码（`include/ms/vm.h`）

```c
typedef enum {
    MS_INTERPRET_OK,
    MS_INTERPRET_YIELD,        // generator yield（OP_YIELD）
    MS_INTERPRET_AWAIT,        // async await（OP_AWAIT）— 新增，EventLoop 专用
    MS_INTERPRET_RUNTIME_ERROR,
} MsInterpretResult;
```

`MS_INTERPRET_AWAIT` 与 `MS_INTERPRET_YIELD` 语义不同：
- **YIELD**：生成器主动产出值，调用方收到值继续运行。
- **AWAIT**：协程注册到 future.waiters 后挂起，EventLoop 不主动 call_soon，等 future resolve 触发。
- 编译器须禁止在 `async fun` 内出现 `yield` 表达式，违者报 `"yield inside async fun"`。

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
        frame->slots[A] = fut->result;   // 已完成：零开销快路径
        DISPATCH();
    }
    if (fut->state == MS_FUTURE_REJECTED) {
        vm_throw(vm, fut->result);       // 抛出异常（走现有 exception 路径 T24）
        return MS_INTERPRET_RUNTIME_ERROR;
    }
    // PENDING：挂起当前协程，注册 waiter
    MsObjCoroutine* coro = vm->current_coroutine;
    if (!coro) {
        runtime_error(vm, "await outside async context");
        return MS_INTERPRET_RUNTIME_ERROR;
    }
    MsWaiter* w = MS_MALLOC(sizeof(MsWaiter));
    w->coro        = coro;
    w->frame_index = coro->ctx.frame_count - 1;  // 记录当前帧，resolve 时安全回写
    w->result_reg  = A;
    w->next        = fut->waiters;
    fut->waiters   = w;
    coro->state    = MS_CORO_SUSPENDED;
    return MS_INTERPRET_AWAIT;  // 区别于 generator yield；EventLoop 不入就绪队列
}
```

## gather() 内建原生函数

```ms
var results = await gather([fut1, fut2, fut3])
```

`gather` 创建父 future，所有子 future resolve 后一并 resolve；任一 reject 立即 reject 父 future（fail-fast）。

### 上下文结构（`src/vm_natives.c`）

```c
typedef struct {
    MsObjFuture* parent;   // 父 future，全部完成时 resolve
    MsValue*     results;  // results[i] = 第 i 个子 future 的结果（MS_MALLOC）
    int          total;    // 子 future 总数
    int          remaining;// 尚未完成的子 future 数
} MsGatherCtx;
```

### 实现

```c
// gather 子 future 完成时的回调（挂入 future.waiters 的特殊 waiter）
static void gather_on_resolve(MsVM* vm, MsGatherCtx* ctx, int index, MsValue result) {
    if (ctx->parent->state != MS_FUTURE_PENDING) return;  // 已因 reject 提前退出
    ctx->results[index] = result;
    if (--ctx->remaining == 0) {
        // 全部完成：把 results 包装成 ObjList resolve 父 future
        MsObjList* lst = ms_obj_list_from_array(vm, ctx->results, ctx->total);
        ms_future_resolve(vm, ctx->parent, MS_OBJECT_VAL((MsObject*)lst));
        MS_FREE(ctx->results);
        MS_FREE(ctx);
    }
}

static void gather_on_reject(MsVM* vm, MsGatherCtx* ctx, MsValue error) {
    if (ctx->parent->state != MS_FUTURE_PENDING) return;
    ms_future_reject(vm, ctx->parent, error);
    // results/ctx 在 parent reject 后不再使用；可延迟到 GC 回收
}

static MsValue native_gather(MsVM* vm, int argc, MsValue* argv) {
    MsObjList* list = MS_AS_LIST(argv[0]);
    int n = list->items.count;

    MsObjFuture* parent = ms_obj_future_new(vm);
    if (n == 0) {
        // 空列表：立即 resolve 空数组
        MsObjList* empty = ms_obj_list_new(vm);
        ms_future_resolve(vm, parent, MS_OBJECT_VAL((MsObject*)empty));
        return MS_OBJECT_VAL((MsObject*)parent);
    }

    MsGatherCtx* ctx = MS_MALLOC(sizeof(MsGatherCtx));
    ctx->parent    = parent;
    ctx->results   = MS_MALLOC(sizeof(MsValue) * n);
    ctx->total     = n;
    ctx->remaining = n;

    for (int i = 0; i < n; i++) {
        MsObjFuture* f = (MsObjFuture*)MS_AS_OBJECT(list->items.data[i]);
        if (f->state == MS_FUTURE_RESOLVED) {
            // 快路径：已完成子 future 直接填结果
            gather_on_resolve(vm, ctx, i, f->result);
        } else if (f->state == MS_FUTURE_REJECTED) {
            gather_on_reject(vm, ctx, f->result);
            break;
        } else {
            // PENDING：注册 gather waiter（ms_future_add_gather_waiter 见 object.c）
            ms_future_add_gather_waiter(vm, f, ctx, i);
        }
    }
    return MS_OBJECT_VAL((MsObject*)parent);
}
```

`ms_future_add_gather_waiter`（`src/object.c`）：在 `f->waiters` 链表末尾追加一个特殊 waiter，resolve/reject 时分别调用 `gather_on_resolve`/`gather_on_reject`（可用 `MsWaiter.type` 字段区分普通 await waiter 与 gather waiter）。

## 验证

- `tests/unit/test_future.c`：
  - pending future + resolve → value 可取
  - reject → 异常传播
  - 多 waiter 注册 + resolve 后全部唤醒
- `tests/fixtures/async_future_manual.ms`：手动 resolve future 验证 await 语义
