# ASYNC-04: EventLoop — 单线程调度器

## 职责

EventLoop 是 async/await 的调度核心：

1. **就绪队列**（ready queue）：立即可执行的协程 FIFO 队列
2. **Timer 堆**（min-heap by deadline）：`sleep(ms)`、超时等
3. **Reactor 轮询**：IO 事件就绪时把对应 future resolve，触发 waiter 进就绪队列
4. **运行循环**：`run_until_complete(future)` 驱动整个 async 生命周期

## 数据结构（`include/ms/event_loop.h`）

```c
// Timer 堆节点
typedef struct {
    uint64_t deadline_ms;     // 绝对时间（ms from epoch 或 monotonic）
    MsObjFuture* future;      // 到期后 resolve 此 future
} MsTimerEntry;

typedef struct MsEventLoop {
    // 就绪队列（双端队列，FIFO）
    MsObjCoroutine** ready;
    int ready_head, ready_tail, ready_cap;

    // Timer 最小堆
    MsTimerEntry* timers;
    int timer_count, timer_cap;

    // Reactor 句柄（跨平台，见 ASYNC-05）
    MsReactor reactor;

    // 停止标志
    bool stopped;

    // 引用的 VM（用于 future resolve 回调）
    MsVM* vm;
} MsEventLoop;
```

## API（`include/ms/event_loop.h`）

```c
void ms_loop_init(MsEventLoop* loop, MsVM* vm);
void ms_loop_destroy(MsEventLoop* loop);

// 立即调度（推入就绪队列）
void ms_loop_call_soon(MsEventLoop* loop, MsObjCoroutine* coro);

// N 毫秒后 resolve future（Timer）
void ms_loop_call_later(MsEventLoop* loop, uint64_t delay_ms, MsObjFuture* fut);

// 运行直到 future resolved/rejected
MsInterpretResult ms_loop_run_until_complete(MsEventLoop* loop, MsObjFuture* fut);
```

## 核心循环（`src/event_loop.c`）

```c
MsInterpretResult ms_loop_run_until_complete(MsEventLoop* loop, MsObjFuture* root) {
    while (!loop->stopped) {
        // 1. 运行所有就绪协程
        while (loop->ready_head != loop->ready_tail) {
            MsObjCoroutine* coro = ready_dequeue(loop);
            MsValue out;
            MsInterpretResult r = ms_vm_coro_resume(loop->vm, coro, MS_NIL_VAL(), &out);
            if (r == MS_INTERPRET_RUNTIME_ERROR) return r;
            // YIELD/OK: coroutine 自己会在 OP_AWAIT 时注册到 future waiter
        }

        // 2. 检查到期 Timer
        uint64_t now = ms_monotonic_ms();
        while (loop->timer_count > 0 && loop->timers[0].deadline_ms <= now) {
            MsTimerEntry entry = timer_heap_pop(loop);
            ms_future_resolve(loop->vm, entry.future, MS_NIL_VAL());
            // resolve 触发 waiter 进就绪队列，下一轮循环处理
        }

        // 3. 若 root future 已完成，退出
        if (root->state != MS_FUTURE_PENDING) break;

        // 4. 若就绪队列空，poll IO（阻塞至下一个 timer 或最多 1 秒）
        if (loop->ready_head == loop->ready_tail) {
            uint64_t timeout_ms = next_timer_deadline(loop, now);
            ms_reactor_poll(&loop->reactor, timeout_ms);
            // reactor 回调会 call ms_future_resolve → 推入就绪队列
        }
    }
    return root->state == MS_FUTURE_RESOLVED
        ? MS_INTERPRET_OK
        : MS_INTERPRET_RUNTIME_ERROR;
}
```

## Timer 实现细节

### 最小堆

```c
static void timer_heap_push(MsEventLoop* loop, MsTimerEntry e) {
    // grow if needed
    loop->timers[loop->timer_count++] = e;
    sift_up(loop->timers, loop->timer_count - 1);
}

static MsTimerEntry timer_heap_pop(MsEventLoop* loop) {
    MsTimerEntry top = loop->timers[0];
    loop->timers[0]  = loop->timers[--loop->timer_count];
    sift_down(loop->timers, 0, loop->timer_count);
    return top;
}
```

### mslang `sleep(ms)` 原生函数

```c
// src/vm_natives.c
static MsValue native_sleep(MsVM* vm, int argc, MsValue* argv) {
    uint64_t delay = (uint64_t)MS_AS_INT(argv[0]);
    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_loop_call_later(vm->event_loop, delay, fut);
    return MS_OBJECT_VAL((MsObject*)fut);
}
// 用法：await sleep(100)  -- 挂起 100ms
```

## EventLoop 与 VM 的集成

`MsVM`（`include/ms/vm.h`）加字段：

```c
MsEventLoop* event_loop;  // NULL = no async context
```

`ms_vm_init`（`src/vm.c`）默认不初始化 event_loop（惰性）；  
`run_until_complete(fut)` 原生函数按需创建：

```c
static MsValue native_run_until_complete(MsVM* vm, int argc, MsValue* argv) {
    if (!vm->event_loop) {
        vm->event_loop = ms_allocate(vm, sizeof(MsEventLoop));
        ms_loop_init(vm->event_loop, vm);
    }
    MsObjFuture* fut = MS_AS_FUTURE(argv[0]);
    MsInterpretResult r = ms_loop_run_until_complete(vm->event_loop, fut);
    return r == MS_INTERPRET_OK ? fut->value : MS_NIL_VAL();
}
```

## GC 根

`vm_gc.c` 的 mark roots 需要 trace EventLoop 中的活跃对象：

```c
if (vm->event_loop) {
    // mark 就绪队列中的协程
    for (int i = ...; i < ...; i++)
        ms_mark_object(vm, (MsObject*)loop->ready[i]);
    // mark timer heap 中的 future
    for (int i = 0; i < loop->timer_count; i++)
        ms_mark_object(vm, (MsObject*)loop->timers[i].future);
}
```

## 验证

```
tests/unit/test_event_loop.c：
  - call_soon → 协程被 resume
  - call_later(50ms) → 50ms 后 future resolve
  - run_until_complete(future) 在 future resolve 后退出
  - 多个并发 sleep：正确按 deadline 顺序到期

tests/fixtures/async_sleep_seq.ms：
  async fun main() {
      await sleep(10)
      await sleep(10)
      print("done")
  }
  run_until_complete(main())

tests/fixtures/async_gather.ms：
  async fun main() {
      var results = await gather([sleep(30), sleep(10), sleep(20)])
      print("all done")
  }
  run_until_complete(main())
```
