# 24 调度器与事件循环

M:N work-stealing 调度，Go 式 G/M 结构，netpoller 一体化。语言层语义见 `08-concurrency.md`。

## 1. 核心概念

| 概念 | 对应 | 说明 |
|------|------|------|
| G（goroutine） | MsObjCoroutine | 独立可增长栈 + 执行帧；脚本层一切代码皆 G |
| M（worker） | 平台线程 × N | 执行 G 的 OS 线程；默认 CPU 核数 |
| runq | 本地双端队列 | 每 M 一个；窃取用 |
| netpoller | reactor 线程 | epoll / kqueue / IOCP 统一接口（src/reactor/） |
| timers | 全局定时器堆 | 最小堆，到期 G 置 runnable |
| bpool | blocking pool | 阻塞系统调用替身线程（≤4） |

## 2. G 状态机

```
        ┌───────── runq ─────────┐
        ▼                        │
   RUNNING ──yield──▶ RUNNABLE ──┘
      │  │
await/chan阻塞/net IO   sleep/timer
      ▼                    ▼
   WAITING ◀─────────── SLEEPING（定时唤醒）
      │ 事件就绪/对端到达
      └────▶ RUNNABLE
   RUNNING ──return──▶ DEAD（join/丢弃）
```

## 3. 栈管理

- 初始 8KB 连续栈；满则 ×2 复制迁移（帧指针重定向：栈图 + 返回地址修正）；上限 1GB（StackOverflowError）。
- 栈扫描（GC root）：frame map 逐帧精确枚举 MsValue 槽。
- async 函数与生成器**不用独立栈**：编译为状态机 chunk（RESUME 重入），栈需求归并进宿主 G——async fn 调用廉价，v1 即高并发友好。

## 4. 调度策略

- 本地 runq 取 G；空则：(1) 全局 runq → (2) 随机窃取其他 M 的半队 → (3) netpoller 非阻塞探询 → (4) 休眠（条件变量，事件源唤醒）。
- 主 G 结束后：默认等 `runtime.backgroundWaitMs`（0，立即退出；可配置等待后台 goroutine/Future）。
- 抢占：`CHECK_SAFEPOINT` 预算耗尽 → G 让出（协作式，粒度 ~1ms 指令预算）；无信号异步抢占（v1 不做，文档化长循环仍会分让出点，回边必插检查）。
- 公平性：同优先级 FIFO + 时间片轮转；select 随机性在语言层保证（`08` §3）。

## 5. channel 实现

```c
typedef struct MsObjChannel {
  MsObj  head;
  MsRing buf;          // 环形缓冲（容量 cap，cap=0 无缓冲）
  Wq     sendQ, recvQ; // 等待 G 队列（FIFO，唤醒保证公平）
  bool   closed;
  Mutex  mu;           // 细粒度锁：仅保护本 channel
} MsObjChannel;
```

- 无缓冲 send：对端 recvQ 有等待者 → **直接交接**（零拷贝槽移交）并唤醒；否则入 sendQ 睡眠。
- 缓冲 send：buf 满 → 入 sendQ；否则入 buf。recv 对称。
- close：唤醒两队列全部等待者（recv 排空语义 / send 抛 ChannelError）。
- select：SELECT_SETUP 登记 n 个 channel 等待项（每 channel 挂一次性票据）→ SELECT_GO：任一就绪（或 default）→ SELECT_COMMIT 撤销其余票据、提交选中分支；多就绪随机。

## 6. netpoller 与阻塞 IO

- 脚本层 IO（net/async 系）注册 interest（fd + 读写）→ G 转 WAITING；就绪回回调置 runnable。Windows：IOCP（IO 线程投递完成包）；Linux：epoll(边缘触发)；macOS：kqueue。
- **阻塞型 stdlib API**（文件 IO、DNS 等）：无法异步化的系统调用下抛 bpool 执行，G 挂 WAITING，完成后回 runnable——脚本层无感，不占用 M。
- `time.sleep`：timers 堆（多 M 各自小顶堆 + 全局溢出堆，Go 式）；到期 Future 完成。

## 7. Future / await 实现

- Future：`{ state: PENDING|DONE|FAILED, result, err, waiters }`；WAITERS 为等待 G 链表。
- AWAIT 指令：Future 完成则续行；否则 G 入 waiters 并切走；完成方逐一唤醒。
- 主 G 的顶层 await 同机制；主 G parked 期间其他 G 照常运行。
- 未 await 的失败 Future：netpoller/调度器收尾扫描登记表，按 uncaught 打印。

## 8. 并发正确性

- GC 接口：G 切换点即 safepoint 候选（`23-gc.md` §6）；parked G 的栈由调度器持有，作为 root。
- 写屏障在多 mutator 下原子操作（CAS 标记位；card 表 atomic 或字节写天然原子）。
- 死锁检测：全员 WAITING/SLEEPING 且 timers/netpoller/bpool 无待办 → 报死锁（`08` §9）。

## 9. 可调参数

| 变量 | 默认 | 说明 |
|------|------|------|
| `MS_SCHED_WORKERS` | CPU 核数 | worker 数 |
| `MS_BPOOL_MAX` | 4 | blocking pool 上限 |
| `runtime.backgroundWaitMs` | 0 | 主 G 退出后等待后台时间 |

- `runtime.goroutineStats()`：各状态计数、runq 深度、窃取次数。
