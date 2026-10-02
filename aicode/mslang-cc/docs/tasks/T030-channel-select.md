# T030 — channel 与 select

- 阶段: P8 调度器（roadmap 期 8）
- 依赖: T029
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/08-concurrency.md` §2 §3 §10、`24-scheduler.md` §5、`21-bytecode.md` §3.8、`10-builtins.md` §1（make/cap）

## 1. 目标与范围

实现 MsObjChannel（环形缓冲 + 等待队列 + 细粒度锁）、MAKE_CHAN/SEND/RECV/CLOSE_CHAN、select 四指令（SETUP/ARM/GO/COMMIT）、`for v in ch`、close/nil 语义、随机选择、`len(ch)/cap(ch)`。出口：roadmap 期 8 完成——08 文档 channel/select 用例 + 压测。

不做：无锁优化（roadmap 风险表：有锁先行，基准驱动后置）、超时子句（用 time.after 惯用法，T032）。

## 2. 实现要点

- 结构（24 §5）：`buf 环形(cap)`、`sendQ/recvQ 等待 G 队列（FIFO 公平）`、`closed`、`mu 细粒度锁（仅本 channel）`。
- 发送/接收（24 §5、08 §2）：无缓冲 send：recvQ 有等待者 → 直接交接（零拷贝槽移交）+ 唤醒；否则入 sendQ 睡眠（rendezvous）。缓冲 send：满则入 sendQ；否则入 buf（FIFO）。recv 对称。
- 阻塞实现：G 转 WAITING 挂队列；唤醒置 RUNNABLE——与调度器状态机对接（被取消的等待需先撤销票据再迁移）。
- 关闭（08 §2）：唤醒两队列全部；recv 排空后得 nil（不阻塞不抛错）；向已关闭发送/重复 close 抛 ChannelError；`close(nil)` 抛 ChannelError。
- nil channel（08 §2）：发送与接收永久阻塞（G 挂 WAITING 无票据——死锁检测消费此态）。
- select（24 §5）：SELECT_SETUP 登记 n 项（每 channel 挂一次性票据）→ SELECT_GO（任一就绪或 default）→ SELECT_COMMIT 撤销其余票据提交分支；多就绪均匀随机（08 §3：随机源用 xorshift，测试可注入种子确定性验证）；无 default 阻塞。
- `for v in ch`（04 §4、08 §2）：GET_ITER 特判 channel——持续 RECV 直到 closed 且排空。
- `make(chan)` / `make(chan, 8)`（10 §1）：容量 int；`len(ch)` 当前元素数、`cap(ch)` 容量。
- select 体为块或单语句；v1 不作表达式（08 §3）。

## 3. 涉及文件

新增: `src/sched/channel.c/.h`、`src/vm/select_ops.c`、`tests/unit/test_channel.c`、`tests/fixtures/chan/*.ms`
修改: `src/compiler/compiler.c`（send/recv/select 编码）、`src/vm/vm.c`（MAKE_CHAN/SEND/RECV/CLOSE_CHAN/SELECT_* 执行）、`src/sched/sched.c`（等待队列 G 迁移、死锁判定接 channel 态）

## 4. 验收标准（DoD）

- [ ] 08 §2 全语义表逐行用例通过（缓冲/无缓冲/close/nil/len/cap）
- [ ] 生产者-消费者 + `for v in ch` 典型样例输出正确
- [ ] select：双就绪随机（种子注入确定性）、default、多 case 提交撤销正确
- [ ] 无缓冲 rendezvous：交替顺序断言（同步会合）
- [ ] ChannelError 三形态（closed send/dup close/close(nil)）可捕获
- [ ] 100 goroutine × channel ping-pong 压测无死锁无崩溃（MSan/ASan 可选构建）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_channel.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 缓冲 FIFO | cap 4 顺序入出 | 保序 |
| 满阻塞唤醒 | 先填满再异步 recv | 唤醒后送入 |
| rendezvous | 无缓冲 send/recv | 双方就绪才推进（时序断言） |
| close 排空 | 缓冲有 2 后 close | recv 得 2 值后恒 nil |
| closed send | 向关闭发 | ChannelError |
| dup close | 二次 close | ChannelError |
| select 随机 | 双就绪种子化 N 轮 | 均匀性统计（卡方粗检） |
| select 撤销 | 选中 A 后 B 就绪 | B 票据失效不触发 |
| for in chan | close 前送 3 | 迭代 3 次停 |

### 5.2 ms fixtures `tests/fixtures/chan/`

- `producer_consumer.ms`：

```ms
var jobs = make(chan, 8)
var results = make(chan)
for i in range(4) {
    go func() { results <- fib(i * 5) }()
}
for _ in range(4) { print(<-results) }   // 0 5 55 610（顺序经排序 golden 固定）
```

（输出确定性：收集后 sorted 输出）

- `select_basic.ms`：双 channel + default + 超时占位（轮询形态）
- `close_semantics.ms`：close 排空/ChannelError 三形态 try/catch
- `ping_pong.ms`：双 goroutine 100 次往返计数
- `for_in_chan.ms`：00-overview §3 尾部并发段（fib + results + await time 前半，time 部分留 T032 复跑）

## 6. 风险与备注

- select 票据撤销的竞态：COMMIT 与对端同时就绪需在 channel 锁内原子判定（锁内短临界区，不做无锁）。
- 死锁检测闭环：全员 WAITING 且无 timer/netpoller/bpool/就绪 channel → 退出码 2（08 §9）——channel 态接入 T028 的判定框架，本任务用例 `deadlock.ms`（`.code`=2）。
