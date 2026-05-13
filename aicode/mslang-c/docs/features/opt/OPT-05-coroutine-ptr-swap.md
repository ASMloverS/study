# OPT-05: 协程 yield/resume 改指针交换

## 背景

当前协程 resume/yield 实现（`src/vm.c:281-334`）在 host VM 与 coroutine 之间来回 **memcpy 整段 frames 数组**：

```c
// ms_vm_coro_resume — vm.c:292
memcpy(vm->frames, coro->frames, coro->frame_count * sizeof(MsCallFrame));
// ... 运行 ... 
// MS_OP_YIELD — vm.c:~1755
memcpy(coro->frames, vm->frames, vm->frame_count * sizeof(MsCallFrame));
```

`MsCallFrame` 大小约 `48~72B`（含 `ip`、`slots`、`closure`、deferred 数组）。  
每次 yield/resume 拷贝量 = `frame_count × sizeof(MsCallFrame)`，随调用栈深度线性增长。  
在协程密集场景（ping-pong、生产者-消费者、async gather）下是明显瓶颈。

## 目标

**coroutine 永久拥有自己的 `stack` / `frames` 数组**；  
host VM 的 `stack/stack_top/frames/frame_count/open_upvalues` 只是一组指针——  
yield/resume 时只需交换指针，**O(1) 常数开销**，与调用深度无关。

## 状态分析

`MsObjCoroutine`（`include/ms/object.h:228-244`）已有独立字段（与代码一致）：

```c
MsCoroState     state;
MsObjClosure*   closure;
MsValue*        stack;
int             stack_size;       /* 已分配的 slot 数 */
MsValue*        stack_top;
struct MsCallFrame* frames;
int             frame_count;
int             frame_capacity;
MsObjUpvalue*   open_upvalues;
MsValue         yield_value;
```

`MsVM`（`include/ms/vm.h`）有：

```c
MsValue stack[MS_STACK_SIZE];   // 静态数组！
MsValue* stack_top;
MsCallFrame frames[MS_FRAMES_MAX]; // 静态数组！
int frame_count;
MsObjUpvalue* open_upvalues;
MsObjCoroutine* current_coroutine;
```

**关键问题**：`MsVM.stack` 和 `MsVM.frames` 是**静态数组**（不是指针），无法直接交换。  
需要将 `MsVM` 的栈改为指针，或引入"当前执行上下文"层。

## 设计

### 方案：引入 `MsExecCtx`（执行上下文）

```c
// include/ms/vm.h
typedef struct {
    MsValue* stack;
    MsValue* stack_top;
    MsCallFrame* frames;
    int frame_count;
    int frame_capacity;
    MsObjUpvalue* open_upvalues;
} MsExecCtx;

struct MsVM {
    // host 线程的静态缓冲区
    MsValue host_stack[MS_STACK_SIZE];
    MsCallFrame host_frames[MS_FRAMES_MAX];
    MsExecCtx host_ctx;   // 指向 host_stack/host_frames

    // 当前活跃上下文（host 或某个协程）
    MsExecCtx* ctx;       // 热路径只读这个

    MsObjCoroutine* current_coroutine;
    // ... 其余字段不变 ...
};
```

`vm->ctx->stack`、`vm->ctx->frames` 替换热路径中 `vm->stack`、`vm->frames`。  
`frame = &vm->ctx->frames[vm->ctx->frame_count - 1]` 等。

### MsObjCoroutine 对应调整

```c
// object.h — coroutine 也持有一个 MsExecCtx
typedef struct MsObjCoroutine {
    MsObject obj;
    MsCoroState state;
    MsObjClosure* closure;
    MsExecCtx ctx;       // 协程的执行上下文（含指针）
    MsValue* stack_buf;  // heap 分配的栈缓冲区
    MsValue yield_value;
} MsObjCoroutine;
```

`ms_obj_coroutine_new`（`object.c:431-445`）分配 `stack_buf`（`MS_CORO_STACK_SIZE * sizeof(MsValue)`）  
并让 `coro->ctx.stack = coro->stack_buf`。

### Resume/Yield（指针交换）

```c
// src/vm.c — ms_vm_coro_resume（原 vm.c:281-334）
MsInterpretResult ms_vm_coro_resume(MsVM* vm, MsObjCoroutine* coro, MsValue sent, MsValue* out) {
    assert(coro->state == MS_CORO_CREATED || coro->state == MS_CORO_SUSPENDED);

    // 保存 host 上下文（仅保存指针，O(1)）
    MsExecCtx* host_ctx = vm->ctx;

    // 切换到协程上下文
    vm->ctx = &coro->ctx;
    vm->current_coroutine = coro;

    // 送入值（必须在设置 RUNNING 之前保存原状态）
    MsCoroState prev_state = coro->state;  /* ← 先保存，再修改 */
    coro->state = MS_CORO_RUNNING;

    if (prev_state == MS_CORO_CREATED) {   /* ← 用 prev_state，不是 coro->state */
        /* push sent to coro stack as argument */
    } else {
        coro->ctx.stack_top[-1] = sent;    // yield 表达式的返回值
    }

    MsInterpretResult result = vm_run_inner(vm);

    // 恢复 host 上下文（O(1)）
    vm->ctx = host_ctx;
    vm->current_coroutine = NULL;

    if (result == MS_INTERPRET_YIELD) {
        coro->state = MS_CORO_SUSPENDED;
        *out = coro->yield_value;
    } else {
        coro->state = MS_CORO_DEAD;
        *out = /* 返回值 */;
    }
    return result;
}
```

`MS_OP_YIELD`（`vm.c:1748-1764`）只需：

```c
case MS_OP_YIELD: {
    MsObjCoroutine* coro = vm->current_coroutine;
    coro->yield_value = frame->slots[A];
    return MS_INTERPRET_YIELD;  // 上下文切换由 ms_vm_coro_resume 的 vm_run_inner 返回完成
}
```

### GC 适配

`vm_gc.c` mark 路径需要 trace `vm->ctx`（当前活跃上下文的 stack/frames）  
以及所有 **suspended** 协程的 `coro->ctx`。  
原有 `ms_obj_coroutine_mark`（`vm_gc.c:129-131`）已遍历 `coro->stack/frames`，  
替换为遍历 `coro->ctx.stack/ctx.frames` 即可。

### Coroutine 析构

`object.c:379-386` — 释放 `coro->stack_buf`（heap 分配的栈缓冲区）。

## 热路径影响

| 位置 | 改动 |
|---|---|
| `vm.c` 所有 `vm->stack` | → `vm->ctx->stack` |
| `vm.c` 所有 `vm->frames` | → `vm->ctx->frames` |
| `vm.c` 所有 `vm->frame_count` | → `vm->ctx->frame_count` |
| `vm.c` 所有 `vm->stack_top` | → `vm->ctx->stack_top` |
| `vm.c` 所有 `vm->open_upvalues` | → `vm->ctx->open_upvalues` |

可用全局搜索替换，配合编译器检查无遗漏（`vm.c` 约 1836 行，替换点多但机械）。

## 验证

新增 `benchmarks/cases/coro_pingpong.ms`：

```
fun* ping(other) {
    var i = 0
    while i < 100000 {
        resume(other, i)
        yield i
        i = i + 1
    }
}
// 两个协程互 resume 100k 次
```

预期 3–5× 提速（消除 memcpy）。

```bash
cmake --build build && cd build && ctest --output-on-failure
python ../benchmarks/run_all.py --compare baseline.json --runs 5
```

conformance 100% 通过，重点看 `tests/conformance/` 中含协程的测试。
