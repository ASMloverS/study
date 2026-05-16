# async/await 设计索引

| 状态 | 文件 | 说明 |
|---|---|---|
| ✅ | [ASYNC-01-overview.md](ASYNC-01-overview.md) | 总体架构与实施顺序 |
| ✅ | [ASYNC-02-frontend.md](ASYNC-02-frontend.md) | Scanner token + Compiler 前端 |
| ⬜ | [ASYNC-03-future-opcode.md](ASYNC-03-future-opcode.md) | ObjFuture + OP_AWAIT |
| ⬜ | [ASYNC-04-event-loop.md](ASYNC-04-event-loop.md) | EventLoop 调度器 |
| ⬜ | [ASYNC-05-reactor.md](ASYNC-05-reactor.md) | 跨平台 Reactor（epoll / kqueue / IOCP）|
| ⬜ | [ASYNC-06-tcp-socket.md](ASYNC-06-tcp-socket.md) | TCP Socket 绑定 |

> ⬜ 待实现 · 🚧 进行中 · ✅ 完成

## 前置依赖

| 依赖 | 说明 |
|---|---|
| **OPT-05**（协程指针交换）| async 底层依赖 ExecCtx 切换机制，需先落地 |
| **T25**（生成器协程）| `MsObjCoroutine` 结构需稳定；async 复用同一对象类型 |

所有协程字段（`coro->ctx.stack`/`frames`）以 OPT-05 最终落地版本为准；  
ASYNC 文档中的字段访问路径假定 OPT-05 已完成。

## v1 不做

- `cancel()` — Future 无取消语义，需明确告知用户
- `async for` / `async with` 语法糖
- 多线程、SSL/TLS、UDP、文件异步 IO
