# T031 — async/await 协程

- 阶段: P9 async（roadmap 期 9）
- 依赖: T029
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/08-concurrency.md` §4 §5 §7、`05-functions.md` §8、`24-scheduler.md` §3 §7、`21-bytecode.md` §3.6（RESUME）

## 1. 目标与范围

实现 async fn（编译为状态机 chunk，无独立栈）、AWAIT 指令与 Future 完整消费链（.then/.catch/all/race）、顶层 await、失败 Future 收尾 uncaught、`await` 非 Future 容错、async 与生成器互斥约束的运行时防线。

不做：net IO 类 async API（T032 起提供事件源）、context 取消（08 §7 v1 仅 channel 信号 + runtime.cancel）。

## 2. 实现要点

- async fn 编译（24 §3）：函数体变换为状态机 chunk——每个 await 点为挂起状态（局部保存到状态槽 + RESUME 重入）；async fn 调用立即返回 Future，体执行至首个 await/完成（08 §4）。
- AWAIT（24 §7）：Future 完成则续行（DONE 取 result；FAILED 重抛异常——08 §4 await 时重抛）；未完成 G 入 waiters 切走；完成方逐一唤醒。
- await 非 Future 值直接返回该值（08 §4 容错）。
- 顶层 await（08 §4）：主 G 同机制挂起，其他 G 照常运行；顶层 await 阻塞后续顶层语句直至完成。
- `.then(fn)/.catch(fn)`（08 §4）：链式回调——完成后 G 投递执行回调（回调异常进链下一环）。
- `all(fs)`（08 §4）：全部完成聚合 list；任一失败立即失败；`race(fs)` 首个完成——builtin all 的 Future 列表重载（T017 预留点启用）与 race 激活。
- 失败 Future 收尾（24 §7）：未 await 且无 .catch 的 FAILED Future 在调度器收尾扫描按 uncaught 打印（08 §4）。
- `go asyncFn()`（08 §5）：合法——Future 无人 await 即并发执行。
- 互斥：async 内 yield 非法 / 生成器内 await 非法（Binder 已拒，运行时再防线断言）。
- await 在普通函数内禁止（静态约束，05 §8）；await 可用于任何 goroutine 内的 async 函数体与模块顶层（08 §5）。

## 3. 涉及文件

新增: `src/vm/async_ops.c/.h`（await/then/all/race）、`tests/unit/test_async.c`、`tests/fixtures/async/*.ms`
修改: `src/compiler/compiler.c`（async 状态机变换）、`src/sched/future.c`（then/catch/收尾扫描）、`src/vm/builtins.c`（all/race 重载）、`src/vm/generator.c`（共用 RESUME 协程核）

## 4. 验收标准（DoD）

- [ ] 08 §4 全部示例语义通过（fetchAll 并发发起 + await all）
- [ ] async 调用立即返回 Future、体执行至首个 await 挂起（副作用计数断言）
- [ ] await 失败 Future 重抛可捕获；失败未 await Future 收尾打印
- [ ] then/catch 链顺序正确；then 内异常进 catch
- [ ] all 任一失败立即失败；race 首个完成（时序注入确定性）
- [ ] 顶层 await 阻塞后续语句 + 后台 G 照常运行（输出顺序断言）
- [ ] `await 42` 容错返回 42

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_async.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 立即返回 | 调 async fn | Future + 体推进到首个 await |
| await 续行 | 手工完成 Future | result 传递 |
| 失败重抛 | FAILED + await | 异常在 await 点抛出 |
| then 链 | then→then→catch | 顺序执行 |
| all | 3 Future 1 失败 | 整体立即失败 |
| race | 2 Future 先后 | 首个结果 |
| 非 Future | await 42 | 42 |
| 互斥防线 | async 内 yield | 编译期拒（复跑断言） |

### 5.2 ms fixtures `tests/fixtures/async/`

- `basic.ms`：

```ms
async func add(a, b) -> a + b
var f = add(1, 2)
print(type(f))            // future
print(await f)            // 3
print(await add(2, 3))    // 5
print(await 42)           // 42
```

- `fetch_all.ms`：`async func fetch(i)`（模拟：立即完成 Future）+ `[fetch(u) for u in range(5)]` + `await all` 汇总（00-overview 风格）
- `then_catch.ms`：链式 + 失败捕获
- `failure_unawaited.ms`：失败 Future 不 await → 收尾 uncaught 打印（stderr 匹配）
- `toplevel.ms`：顶层 await + 后台 go 交替输出顺序

## 6. 风险与备注

- 状态机变换是编译器最复杂变换：实现策略 = async 体先编译常规 chunk，再做"await 点切割 + 状态槽提升"后处理；`ms dump --chunk` 输出状态表供人工核验（快照固化）。
- all/race 的结果顺序：all 按 futures 列表原序聚合（非完成序）——文档化并用例锁定。
