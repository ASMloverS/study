# ASYNC-01: async/await 总体设计

## 目标

在 mslang 中引入 **Python 风格**的 async/await 并发模型：

- `async fun` 函数调用后返回一个 **Future**，而非立即执行
- `await` 挂起当前协程，等待 Future 完成
- 单线程**事件循环（EventLoop）**调度所有 async 任务
- IO 后端通过跨平台 **Reactor** 抽象（epoll/kqueue/IOCP）

## 架构层次

```
mslang 代码层
   async fun foo() { await bar() }
          ↓ 编译为协程 + OP_AWAIT
VM 层（src/vm.c）
   OP_AWAIT: 检查 Future 状态；未完成则挂起协程 → 把控制权交还 EventLoop
          ↓
EventLoop（src/event_loop.c）
   就绪队列 + Timer 堆 + Reactor 轮询
          ↓
Reactor（src/reactor_*.c）
   epoll(Linux) / kqueue(BSD/macOS) / IOCP(Windows)
          ↓
OS IO（TCP socket、timer 等）
```

## await 路径详图（与 OPT-05 ExecCtx 的协作）

```
调用 async fun foo()
    ↓ call_value 检测 is_async=true
    ↓ ms_obj_future_from_async → 创建 ObjFuture(state=PENDING, coro=NEW)
    ↓ ms_loop_call_soon(coro)  → coro 入就绪队列
    ↓ 调用方得到 ObjFuture，可立即 await 或稍后 await

EventLoop run → ready_dequeue(coro)
    ↓ ms_vm_coro_resume(coro)
    ↓ OPT-05 指针交换：host ctx ↔ coro ctx（保存/恢复 frame/stack 指针）
    ↓ coro 执行至 OP_AWAIT x
         ├─ x 已 RESOLVED → frame->slots[A] = x.result；继续执行（零开销）
         ├─ x 已 REJECTED → vm_throw(x.result)；return RUNTIME_ERROR
         └─ x PENDING →
              创建 MsWaiter { coro, frame_index, result_reg }
              追加到 x.waiters
              coro->state = SUSPENDED
              return MS_INTERPRET_AWAIT
                  ↓
              OPT-05 指针交换回 host ctx（EventLoop 恢复）
              EventLoop 继续 poll 其他就绪协程 / IO / Timer

x resolve（来自 IO 回调 或 另一协程完成）
    ↓ ms_future_resolve(vm, x, result)
    ↓ 遍历 x.waiters：
         w->coro->ctx.frames[w->frame_index].slots[w->result_reg] = result
         ms_loop_call_soon(w->coro)
    ↓ 下一轮 EventLoop → coro 出队 → ms_vm_coro_resume
    ↓ OPT-05 切回 coro ctx，从 OP_AWAIT 后一条指令继续
```

"挂起"等价于 OPT-05 的 ExecCtx 指针交换（`MsObjCoroutine.ctx` ↔ EventLoop 持有的 host context），不是 `setjmp`/`longjmp`。

## 与现有协程的关系

现有 `fun*` 生成器协程（T25）是**手动调度**的（用户显式 `resume`/`yield`）。  
async/await 复用相同的底层机制（`MsObjCoroutine` + 指针交换，见 OPT-05），  
差异在于**谁来 resume**：

| 维度 | `fun*` 生成器 | `async fun` |
|---|---|---|
| resume 方 | 用户代码 `resume(co, val)` | EventLoop（透明） |
| 挂起触发 | `yield expr` | `await future` |
| 返回类型 | 手动取值 | `Future` 对象 |
| 调度器 | 无 | 单线程 EventLoop |

## 新增关键字

| 关键字 | Token | 说明 |
|---|---|---|
| `async` | `TK_ASYNC` | 修饰函数声明 |
| `await` | `TK_AWAIT` | 表达式前缀 |
| `spawn` | `TK_SPAWN` | 创建并提交 async 任务到 loop（可选 v1） |

## 核心对象

| 对象 | 文件 | 说明 |
|---|---|---|
| `ObjFuture` | object.h / object.c | 异步结果容器（PENDING/RESOLVED/REJECTED + waiters 链表）|
| `ObjSocket` | object.h / object.c | 跨平台 TCP socket（wraps fd/HANDLE）|
| `MsEventLoop` | event_loop.h / event_loop.c | 调度器（就绪队列 + timer 堆）|
| `MsReactor` | reactor.h / reactor_*.c | IO 多路复用抽象 |

## 用户代码示例

```ms
// 简单 await
async fun fetch(url) {
    var conn = await tcp_connect("example.com", 80)
    await conn.write("GET / HTTP/1.0\r\n\r\n")
    var resp = await conn.read(4096)
    return resp
}

// 并发等待（gather）
async fun main() {
    var results = await gather([fetch("a.com"), fetch("b.com")])
    print(results)
}

// 入口
run_until_complete(main())
```

## 实施顺序（依赖关系）

```
OPT-05 (协程指针交换)  ← 基础，async 在此之上
    ↓
ASYNC-02 (Scanner + Compiler 前端)
    ↓
ASYNC-03 (ObjFuture + OP_AWAIT)
    ↓
ASYNC-04 (EventLoop 内核)
    ↓
ASYNC-05 (Reactor 跨平台)
    ↓
ASYNC-06 (TCP socket 绑定 + 原生函数)
```

## 不在 v1 范围内

- 多线程（async 是单线程的）
- `async for` / `async with` 语法糖
- SSL/TLS
- UDP
- 文件异步 IO（v1 用同步包装，后续用线程池）
