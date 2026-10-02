# T032 — timers/netpoller/bpool 与 time 模块

- 阶段: P9 async（roadmap 期 9）
- 依赖: T028
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/24-scheduler.md` §6、`08-concurrency.md` §6、`26-stdlib.md` §2（time）、`20-architecture.md` §3

## 1. 目标与范围

实现全局/worker 定时器堆（time.sleep/after/tick）、reactor 三平台多路复用封装（epoll/kqueue/IOCP）、blocking pool（≤4）与阻塞 API 下抛机制、死锁检测完整化（退出码 2）、`time` 标准库模块（26 §2 全函数）。出口：roadmap 期 9——`await time.sleep(0.1)` 与 `select { case <-time.after(1) }` 可用；千并发 echo 的基础就绪（net 模块本体归 T043）。

不做：net 模块（Tier2）、time 的 Time 类型 format/parse 全布局（最小集 + iso 预设，完整布局归本任务内——26 §2 要求一并交付）。

## 2. 实现要点

- timers（24 §6）：多 M 各自小顶堆 + 全局溢出堆（Go 式）；`time.sleep(sec)` 返回 Future（到期完成，08 §6）；`time.after(sec)` 返回超时 channel（select 惯用法）；精度毫秒级；`time.tick(sec)` 周期 channel。
- reactor（24 §6）：`src/reactor/` 统一接口 `interest(fd, READ/WRITE) → 回调置 G runnable`；Linux epoll 边缘触发 / macOS kqueue / Windows IOCP（IO 线程投递完成包）；netpoller 线程 1（20 §3）；调度器空闲探询点接 T028 桩。
- bpool（24 §6）：阻塞系统调用替身线程池 ≤4（MS_BPOOL_MAX）；stdlib 阻塞 API（文件 IO/DNS）下抛执行——G 挂 WAITING、完成回 runnable、不占 worker。
- 死锁检测完整（24 §8）：全员 WAITING/SLEEPING 且 timers/netpoller/bpool 无待办 → 打印各 G 栈退出码 2（08 §9）。
- time 模块（26 §2）：now/mono/sleep/after/since/duration 常量（ms s m h）/Time 类型（format parse：Go 布局 `2006-01-02 15:04:05` 最小集 + Py 预设 `iso`）/tick/date 构造/unix 秒毫秒——C 实现注册进模块注册表（T024 机制）。
- 阻塞语义统一（08 §5）：脚本层调阻塞 API → 下抛 bpool + G 挂起，脚本层无感。

## 3. 涉及文件

新增: `src/sched/timers.c/.h`、`src/reactor/reactor.h`、`src/reactor/reactor_epoll.c`、`src/reactor/reactor_kqueue.c`、`src/reactor/reactor_iocp.c`、`src/sched/bpool.c/.h`、`src/stdlib/time.c`、`tests/unit/test_timers.c`、`tests/unit/test_reactor.c`、`tests/fixtures/async_time/*.ms`
修改: `src/sched/sched.c`（探询/死锁判定/收尾）、`src/sched/future.c`（timer Future 完成）、`src/module/builtin_libs.c`（注册 time）

## 4. 验收标准（DoD）

- [ ] `await time.sleep(0.05)` 挂起≥50ms 后续行（时间断言窗口）
- [ ] `select { case <-ch: … case <-time.after(1): }` 超时分支触发（08 §10 惯用法）
- [ ] reactor 三平台接口单测（回环 fd/pipe 就绪回调；IOCP 用命名管道）
- [ ] bpool：阻塞任务执行期间 worker 不减少（并发压测计数）
- [ ] 死锁：全员睡眠无事件源 → 各 G 栈打印 + 退出码 2
- [ ] time：format/parse（Go 布局最小集 + iso）、unix 双精度、since/duration 全过
- [ ] time.after 精度统计（100 次采样误差 < 5ms 中位）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_timers.c`、`test_reactor.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 堆序 | 乱序插入 1000 定时器 | 到期序单调 |
| sleep Future | 50ms | 完成时间 ∈ [50, 55]ms |
| after channel | 10ms | 到期投递值 |
| tick | 3 次 | 间隔近似周期 |
| epoll/kqueue/IOCP | pipe 就绪 | 回调 G 置 runnable |
| bpool 满载 | 5 并发阻塞任务 | 4 并行上限 + 排队 |
| 死锁 | 全 G 睡无源 | 退出码 2 + 栈打印 |

### 5.2 ms fixtures `tests/fixtures/async_time/`

- `sleep_await.ms`：

```ms
var t0 = time.mono()
await time.sleep(0.05)
print(time.since(t0) > 0.04)     // true
print(time.after(0.01) is nil)   // false（是 channel）
```

- `select_timeout.ms`：select + time.after 双分支（超时与就绪两形态）
- `time_module.ms`：format/parse/date/unix 组合（golden）
- `deadlock.ms`：`var ch = make(chan); <-ch`（无对端）→ `.code`=2、stderr 含 goroutine 栈
- `overview_final.ms`：00-overview §3 完整 fib 示例全量运行（fib + Greeter + 推导 + channel 并发 + await sleep）——语言核心里程碑用例

## 6. 风险与备注

- Windows IOCP 与 epoll 语义差异是 roadmap 登记风险：reactor 抽象层接口先以"回环验收单测"三平台同源用例固化（20 §7）。
- `overview_final.ms` 为里程碑：通过即 00-overview 示例全量可运行，进入 conformance 金集并在后续任务 DoD 中作为回归必跑。
