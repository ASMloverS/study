# T028 — 调度器骨架与可增长栈

- 阶段: P8 调度器（roadmap 期 8）
- 依赖: T027
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/24-scheduler.md` §1-§4、`23-gc.md` §6 §10（多 worker 分区）

## 1. 目标与范围

实现 G/M 结构与状态机（RUNNING/RUNNABLE/WAITING/SLEEPING/DEAD）、runq 本地双端队列 + work stealing、worker 线程池（默认 CPU 核数）、可增长栈（8KB 起步 ×2 迁移、上限 1GB、StackOverflowError）、主 G 与退出策略、GC 与调度器的 per-worker 分区复核（young nursery/card 表）。本任务先以"单 G 主协程跑通新骨架"为第一步，再开多 worker。

不做：抢占（T029）、channel（T030）、netpoller（T032）。

## 2. 实现要点

- G（24 §1）：MsObjCoroutine 完整化——独立连续栈 + 帧链 + 状态；主模块体即主 G（08 §1）。
- 状态机（24 §2）：RUNNING→RUNNABLE（yield）→runq；WAITING/SLEEPING 由后续任务的事件源驱动，本任务实现状态字段与迁移 API。
- 调度策略（24 §4）：本地 runq 取 → 全局 runq → 随机窃取半队 → 休眠（条件变量）；本任务事件源少，(3) netpoller 探询点留桩。
- 栈管理（24 §3）：8KB 起步；满则 ×2 复制迁移——帧指针重定向（栈图 + 返回地址修正：每帧记录 frame base 与 chunk 上下文，迁移时按 frame map 改写栈内指针槽）；上限 1GB 抛 StackOverflowError（T018 的固定帧深上限切换为真实栈预算）。
- worker：plat 线程池；主线程也是 worker（20 §3）；`MS_SCHED_WORKERS` 可调（24 §9）；per-worker young nursery（23 §10：worker 间无共享 young）+ card 表分区复核。
- 主 G 退出（24 §4）：主 G 结束后默认立即退出；`runtime.backgroundWaitMs`（0）等待策略字段先行（模块归 T036）。
- GC 接口（24 §8）：G 切换点即 safepoint 候选——切换路径统一调 CHECK_SAFEPOINT 语义入口；parked G 栈由调度器持有作 root（GC roots 遍历扩展：全部 G 栈枚举）。
- 死锁检测占位（24 §8）：全员睡眠且无定时器/IO/就绪 channel → 退出码 2——事件源完整化在 T032，本任务实现判定框架。

## 3. 涉及文件

新增: `src/sched/sched.c/.h`（G/M/runq/stealing）、`src/sched/stack.c/.h`（可增长栈）、`src/sched/worker.c`、`tests/unit/test_sched.c`、`tests/fixtures/sched/*.ms`
修改: `src/vm/vm.c`（执行循环移入 G 上下文、帧深上限切换栈预算）、`src/gc/*.c`（roots 遍历多 G 栈、per-worker nursery）、`src/obj/obj_coroutine.c`

## 4. 验收标准（DoD）

- [ ] 主 G 在新骨架上全量既有 fixtures 通过（语义零回归）
- [ ] 栈增长：深递归（>8KB 帧）正确迁移，1MB 默认协程栈下 fib 递归 / 万级深调用通过
- [ ] StackOverflowError：构造 1GB 逼近用例（栈上限可注入小值加速）捕获成功
- [ ] 多 worker 空转启停、runq 窃取单测（C 层直接投递假 G）
- [ ] GC：多 G 栈 roots 扫描 + per-worker nursery 下 ASan 全绿
- [ ] `MS_SCHED_WORKERS=1/2/N` 行为一致（输出确定性用例）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_sched.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| G 生命周期 | 创建/入队/执行/DEAD | 状态迁移正确 |
| runq FIFO | 投 3 G | 执行序 = 入队序 |
| 窃取 | 两 worker 不均衡投递 | 空闲者偷半队 |
| 栈迁移 | 跨增长阈值帧链 | 帧指针/局部/upvalue 全部有效 |
| 栈上限 | 注入小上限深递归 | StackOverflowError 可捕获 |
| 多 G roots | 两 G 各持对象过 GC | 均保活 |

### 5.2 ms fixtures `tests/fixtures/sched/`

- `deep_recursion.ms`：

```ms
func depth(n) {
    if n == 0 { return 0 }
    return 1 + depth(n - 1)
}
print(depth(100000))       // 100000（栈多次倍增）
```

- `stack_overflow.ms`：无限递归 + try/catch StackOverflowError → `.code`=0 捕获输出（上限注入）
- `main_g.ms`：主 G 语义样例（既有 fixtures 复跑抽样）

## 6. 风险与备注

- 栈迁移是本任务最大风险：迁移正确性依赖 frame map 完备（每帧哪些槽是 MsValue/指针）——chunk 端 map 生成在 T026 精确栈图基础上扩展"含指针槽标注"。
- Windows 线程数 = CPU 核数在大核机器默认偏高：上限 8 封顶（文档注记，`MS_SCHED_WORKERS` 显式覆盖）。
