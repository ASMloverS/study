# T044 — 基准门禁与 v0.1 收尾

- 阶段: P12 发布（roadmap 期 12）
- 依赖: T039、T043
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/29-roadmap.md` §4 §5 §6 §7、`20-architecture.md` §6、`22-object-model.md` §7

## 1. 目标与范围

建立基准套件与回归门禁（benchmarks/ 9 用例 + baseline.json + CI 触发策略）、性能优化评估（quickening/NaN-boxing 评估报告）、v0.1 收尾（conformance ≥300 复核、examples 全可运行、版本策略落地）。

不做：JIT（v1 非目标）、优化的实施（本任务只评估 + 立项建议，显著实施另开任务）。

## 2. 实现要点

- 基准用例（29 §5）：fib、字符串拼接、map 读写、对象字段访问（IC）、channel ping-pong、goroutine spawn 风暴、GC 压力（分配速率扫描）、JSON 序列化、（补充）推导式循环。
- 基准跑器：`benchmarks/run_bench.ms`（依托 `testing.bench`）+ C 侧 wall-clock/指令数（MS_VM_STATS 计数）采样；9 次取中位（29 §5）。
- baseline.json：Release 配置基线（每用例 wall-clock/指令数/GC 暂停分布分位数）；阈值 wall ±5%、指令 ±0.5%、GC 暂停分布回归即失败（29 §5）。
- CI 触发（29 §5）：每 PR 核心前 5 例；每周全量；`ms test` 退出码 2 接线（T039 骨架对齐）。
- quickening 评估（20 §7）：热点指令特化原型（CALL/LOAD 常见形态）在 fib/map 用例 A/B 报告；NaN-boxing（22 §7）：`MS_NAN_BOXING=1` Release 构建 9 用例对比——≥5% 综合提升才建议默认开启（22 §7 判据）。
- v0.1 收尾（29 §4）：
  - conformance ≥300 用例全绿（统计脚本输出在案）
  - examples/ 全可运行（CI 门禁目录）
  - fib(32) 性能记录（对标基线：旧 mslang-c 80% / CPython 3.12 1.5× 目标口径记录，达不成出具差距分析文档）
  - GC P99 指标记录（minor <1ms / major STW <500µs）
  - 千并发 echo 10 分钟稳定记录
- 版本策略落地（29 §7）：`__version__` 与 `ms version` 对齐 0.1.0；`.msc` 指纹含编译器版本复核（T025 断言复跑）。

## 3. 涉及文件

新增: `benchmarks/*.ms`（9+1 用例）、`benchmarks/baseline.json`、`tools/bench_gate.py`（基线比对/报告）、`docs/perf/v0.1-report.md`（评估与差距分析产物）、`examples/*`（补齐可运行示例集）
修改: `.github/workflows/ci.yml`（PR 基准/夜跑全量矩阵）、`CMakeLists.txt`（Release 基准目标）

## 4. 验收标准（DoD）

- [ ] 基准跑器产出结构化结果（json），9 次中位统计正确
- [ ] baseline.json 入库；±5%/±0.5% 阈值门禁可复现（注入劣化 → 退出码 2）
- [ ] PR 核心前 5 例接入 CI；周全量任务就位
- [ ] quickening/NaN-boxing 评估报告落盘（数据 + 建议结论）
- [ ] conformance 用例数 ≥300 全绿（统计输出）
- [ ] examples CI 全绿；v0.1 指标记录齐备（性能/GC/echo 三项）
- [ ] `ms version` == `__version__` == 0.1.0

## 5. 测试计划

### 5.1 基准用例（benchmarks/）

| 用例 | 文件 | 度量 |
|------|------|------|
| fib | `bench_fib.ms`（fib(32)） | wall/指令 |
| 字符串拼接 | `bench_strcat.ms` | wall/分配速率 |
| map 读写 | `bench_map.ms` | wall |
| 字段访问 IC | `bench_ic.ms` | wall/IC 命中率 |
| channel ping-pong | `bench_chan.ms` | wall |
| spawn 风暴 | `bench_spawn.ms` | wall |
| GC 压力 | `bench_gc.ms` | 暂停分位 |
| JSON | `bench_json.ms` | wall |
| 推导式 | `bench_compr.ms` | wall |

### 5.2 ms 验证脚本

- `benchmarks/selfcheck.ms`：跑器正确性自测（固定工作负载的时间单调性/中位计算断言）
- `examples/`（fizzbuzz/fileio/httpd/echo/cli 等代表性 8-10 例）随 CI `ms run` 全跑

## 6. 风险与备注

- 基准机器噪声：CI 基准跑于固定规格 runner（github standard）+ 阈值内统计容差；本地开发 `--bench-only` 不出门禁结论。
- 性能未达目标的处置路径：差距分析文档 + 后续立项（quickening/IC 强化），不阻塞 v0.1 发布（29 §4 为"目标"口径，报告在案即可）。
