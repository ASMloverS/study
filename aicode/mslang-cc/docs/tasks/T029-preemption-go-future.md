# T029 — 抢占与 go 语句/Future

- 阶段: P8 调度器（roadmap 期 8）
- 依赖: T028
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/24-scheduler.md` §4 §7、`08-concurrency.md` §1 §5 §7 §8、`21-bytecode.md` §3.8 §6

## 1. 目标与范围

实现协作式抢占（CHECK_SAFEPOINT 预算让出）、SPAWN 指令与 go 表达式（返回 Future）、异常跨 goroutine 隔离、Future 对象基础（state/result/err/waiters，await 消费归 T031）、`runtime.cancel` 语义与 `runtime.goroutineStats` 埋点。

不做：channel（T030）、async fn（T031）、runtime 模块对外（T036，本任务经调试开关/环境变量暴露）。

## 2. 实现要点

- 抢占（24 §4）：CHECK_SAFEPOINT 编译器插桩完整化（函数入口/回边/CALL 前，21 §6）；预算（~1ms 指令量）耗尽 → G 让出回 runq（RUNNING→RUNNABLE）；与 GC 状态字复用同一检查点（23 §6）。
- go 语句/表达式（08 §5、03 §8）：`go f(args)` / `go func(){...}()` 编译为 SPAWN——新 G（复制闭包环境引用）入调度器；go 是表达式，返回其 Future（await 即 join；弃置即发射后不管）。
- Future（24 §7）：`{state: PENDING/DONE/FAILED, result, err, waiters}`；G 正常结束 → result 填充 + 唤醒 waiters；uncaught 异常 → FAILED 存异常（08 §5：Future 内异常存入，await 时重抛）。
- 异常隔离（07 §5）：goroutine 内 uncaught 仅终止该 G、打印 traceback，不传播主协程；主协程 uncaught 异常终止程序退出码 1。
- 取消（08 §7）：`runtime.cancel(fut)` 对 go-Future 注入 CancelledError 到目标协程（在下一 safepoint 抛出）；对 async Future 无效（返回 false）——实现注入点 = CHECK_SAFEPOINT 检查取消标记。
- 公平性（24 §4）：同优先级 FIFO + 时间片；select 随机性归 T030。
- 主 G 退出策略（24 §4）：默认 0 不等；backgroundWaitMs 字段生效（环境变量 `MS_BG_WAIT_MS` 先行）。
- goroutineStats：各状态计数/runq 深度/窃取次数——`MS_VM_STATS` 输出行。

## 3. 涉及文件

新增: `src/sched/future.c/.h`、`src/vm/spawn.c`、`tests/unit/test_go_future.c`、`tests/fixtures/go/*.ms`
修改: `src/compiler/compiler.c`（CHECK_SAFEPOINT 插桩、go 编译）、`src/vm/vm.c`（预算让出/取消注入）、`src/sched/sched.c`（waiters 唤醒、退出策略）

## 4. 验收标准（DoD）

- [ ] 长热循环不阻塞其他 G（打印交替观测）；预算粒度 ~1ms（统计口径）
- [ ] `go f()` 并发执行完成；`var j = go slow(); ... await j`（await 最小形态先行接通——join 语义）
- [ ] goroutine 内 uncaught 打印 traceback 不影响主 G（退出码 0）
- [ ] Future FAILED：await 时重抛（T031 前以 join 等待 + 轮询断言的 C 用例覆盖）
- [ ] cancel：go-Future 收到 CancelledError 可捕获；async-Future 返回 false
- [ ] 100 goroutine 压测（roadmap 期 8 口径）：spawn 风暴 + 汇总计数正确

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_go_future.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| SPAWN 执行 | go 计数协程 ×N | 计数收敛 |
| 抢占让出 | 两热循环 G | 交替推进（步进计数） |
| Future 完成 | G return 值 | result 就绪、waiters 唤醒 |
| 异常隔离 | G 内 throw | G DEAD + FAILED、进程存活 |
| cancel | 注入取消 | 目标 G 捕获 CancelledError |
| 退出策略 | MS_BG_WAIT_MS=500 | 主 G 后等 500ms 收尾 |

### 5.2 ms fixtures `tests/fixtures/go/`

- `basic.ms`：

```ms
var total = 0
var done = make(chan, 1)   // chan 未实现前用 busy-wait 轮询版本；T030 后升级
var j = go func() { total = 42 }()
await j
print(total)               // 42
```

（注：本任务先以 `go func(){ total = 42 }()` + 主 G 顶层 `await` 最小路径；chan 形态在 T030 复跑更新）

- `isolation.ms`：G 内 throw ValueError → stderr 有 traceback、主输出正常、退出码 0
- `spawn_storm.ms`：100 × go 累加 + 汇总（确定性输出）
- `cancel.ms`：cancel + 捕获 CancelledError

## 6. 风险与备注

- await 在 T031 才完整合法化；本任务 go 返回 Future 的消费以"join 等待"最小实现（顶层 await 走同一路径），语法合法性按 08 §5（顶层 await 合法）先行放开顶层一处。
- 数据竞争立场（08 §8）：spawn_storm 用独立 G 累加各自结果再汇总，避免依赖未定义共享写。
