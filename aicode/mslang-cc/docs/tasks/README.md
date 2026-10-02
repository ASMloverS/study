# mslang 实现任务索引

基于 `docs/language/` 设计文档拆分的实现任务总索引。任务编号按依赖顺序全局递增，每个任务是可独立运行、可测试验证的最小交付单元（对应一次可合并的变更）。

- 任务文档命名：`T0NN-slug.md`（本目录内）
- 状态更新：开工时改任务文档头部与下表"状态"列（仅 emoji，含义见状态图例）；完成须满足该任务 DoD 且全部相关测试通过
- 阶段划分与 `29-roadmap.md` §3 的 12 期一一对应（P0-P12）

## 状态图例

| 状态 | 含义 |
|------|------|
| ⬜ | 未开始：尚未动工 |
| 🚧 | 进行中：已开工未达 DoD |
| ✅ | 已完成：DOD 全部满足、ctest 全绿、已提交 |
| ⛔ | 阻塞：被依赖或外部因素阻塞（在任务文档备注原因） |

## 总表

| 编号 | 任务 | 文档 | 阶段 | 依赖 | 规模 | 状态 |
|------|------|------|------|------|------|------|
| T001 | 项目骨架 | [T001](T001-project-skeleton.md) | P0 | - | M | ⬜ |
| T002 | 测试基础设施 | [T002](T002-test-infra.md) | P0 | T001 | M | ⬜ |
| T003 | Token 定义与诊断 | [T003](T003-token-diagnostic.md) | P1 | T002 | S | ⬜ |
| T004 | Scanner 核心 | [T004](T004-scanner-core.md) | P1 | T003 | M | ⬜ |
| T005 | Scanner 字符串/rune | [T005](T005-scanner-string-rune.md) | P1 | T004 | M | ⬜ |
| T006 | AST 与基础语句 | [T006](T006-ast-basic-statements.md) | P2 | T005 | M | ⬜ |
| T007 | 表达式解析 | [T007](T007-expression-parsing.md) | P2 | T006 | L | ⬜ |
| T008 | 复合语句与函数/类 | [T008](T008-complex-statements.md) | P2 | T007 | L | ⬜ |
| T009 | Binder | [T009](T009-binder.md) | P2 | T008 | M | ⬜ |
| T010 | 指令集与 Compiler | [T010](T010-instruction-compiler.md) | P3 | T009 | L | ⬜ |
| T011 | VM 基础 | [T011](T011-vm-basic-control-flow.md) | P3 | T010 | L | ⬜ |
| T012 | 堆与临时 GC | [T012](T012-heap-basic-gc.md) | P4 | T011 | M | ⬜ |
| T013 | string 对象 | [T013](T013-string-object.md) | P4 | T012 | L | ⬜ |
| T014 | list 与解构赋值 | [T014](T014-list-destructuring.md) | P4 | T013 | M | ⬜ |
| T015 | map / set / tuple | [T015](T015-map-set-tuple.md) | P4 | T013 | L | ⬜ |
| T016 | bigint 与数值塔 | [T016](T016-bigint-numerics.md) | P4 | T012 | L | ⬜ |
| T017 | Tier0 内置与迭代 | [T017](T017-tier0-builtins-iteration.md) | P4 | T013-T016 | L | ⬜ |
| T018 | 函数与闭包 | [T018](T018-functions-closures.md) | P5 | T017 | L | ⬜ |
| T019 | 生成器 | [T019](T019-generators.md) | P5 | T018 | M | ⬜ |
| T020 | class 与继承 mixin | [T020](T020-classes-inheritance.md) | P5 | T018 | L | ⬜ |
| T021 | dunder 全协议 | [T021](T021-dunder-protocol.md) | P5 | T020 | L | ⬜ |
| T022 | Shape 与内联缓存 | [T022](T022-shape-inline-cache.md) | P5 | T021 | M | ⬜ |
| T023 | 异常系统与 defer | [T023](T023-exceptions-defer.md) | P6 | T020 | L | ⬜ |
| T024 | 模块系统 | [T024](T024-modules.md) | P6 | T018 | L | ⬜ |
| T025 | .msc 字节码缓存 | [T025](T025-msc-cache.md) | P6 | T024 | M | ⬜ |
| T026 | 分代 young GC | [T026](T026-generational-young.md) | P7 | T023 | L | ⬜ |
| T027 | 并发标记 major + WeakRef | [T027](T027-concurrent-mark.md) | P7 | T026 | L | ⬜ |
| T028 | 调度器骨架 | [T028](T028-scheduler-core.md) | P8 | T027 | L | ⬜ |
| T029 | 抢占与 go 语句/Future | [T029](T029-preemption-go-future.md) | P8 | T028 | M | ⬜ |
| T030 | channel 与 select | [T030](T030-channel-select.md) | P8 | T029 | L | ⬜ |
| T031 | async/await 协程 | [T031](T031-async-await.md) | P9 | T029 | L | ⬜ |
| T032 | timers/netpoller/time | [T032](T032-timers-netpoller.md) | P9 | T028 | L | ⬜ |
| T033 | ms.h C API 与扩展加载 | [T033](T033-capi-extensions.md) | P10 | T027, T028 | L | ⬜ |
| T034 | Tier1 组 A | [T034](T034-tier1-pure.md) | P10 | T033 | L | ⬜ |
| T035 | Tier1 组 B | [T035](T035-tier1-io.md) | P10 | T032, T033 | L | ⬜ |
| T036 | Tier1 组 C | [T036](T036-tier1-runtime.md) | P10 | T027, T029 | M | ⬜ |
| T037 | ms run 完整化与 REPL | [T037](T037-run-repl.md) | P11 | T024 | M | ⬜ |
| T038 | ms fmt 格式化器 | [T038](T038-fmt.md) | P11 | T009 | L | ⬜ |
| T039 | ms test 与 testing 模块 | [T039](T039-test-command.md) | P11 | T037 | M | ⬜ |
| T040 | ms dump 完整化与 debug TUI | [T040](T040-dump-debug.md) | P11 | T029 | L | ⬜ |
| T041 | ms build 与 .msb bundle | [T041](T041-build-bundle.md) | P12 | T025 | M | ⬜ |
| T042 | ms pkg 与 ms doc | [T042](T042-pkg-doc.md) | P12 | T009, T041 | M | ⬜ |
| T043 | Tier2 标准库 | [T043](T043-tier2-stdlib.md) | P12 | T032, T035 | L | ⬜ |
| T044 | 基准门禁与 v0.1 收尾 | [T044](T044-benchmark-release.md) | P12 | T039, T043 | M | ⬜ |

## 依赖关系

```text
P0   T001 → T002
P1   T002 → T003 → T004 → T005
P2   T005 → T006 → T007 → T008 → T009
P3   T009 → T010 → T011
P4   T011 → T012 ┬→ T013 ┬→ T014
                 │       └→ T015
                 └→ T016
     T013+T014+T015+T016 → T017
P5   T017 → T018 ┬→ T019
                  └→ T020 → T021 → T022
P6   T018 → T024 → T025
     T020 → T023
P7   T023 → T026 → T027
P8   T027 → T028 ┬→ T029 ┬→ T030
                  │       └→ T031
                  └→ T032
P9   （T031/T032 见上，依赖 P8 调度器）
P10  T027+T028 → T033 ┬→ T034
                      └→ T035
     T027+T029 → T036
P11  T024 → T037 → T039
     T009 → T038
     T029 → T040
P12  T025 → T041 → T042
     T032+T035 → T043
     T039+T043 → T044
```

说明：依赖为"最小前置"，不禁止更早任务先行（如 T016 bigint 仅依赖 T012 堆，可与 T013 并行）。关键汇合点：T017（内置函数，需四种容器齐备）、T023（异常，需 class）、T033（C API，需 GC 与调度器稳定面）。

## 与 roadmap 的映射

| roadmap 期（29-roadmap.md §3） | 任务 | 出口标准对照 |
|------|------|------|
| 0 骨架 | T001-T002 | 三平台空壳构建绿 |
| 1 词法 | T003-T005 | 01 文档词法用例全覆盖 |
| 2 语法 | T006-T009 | conformance 语法树快照匹配 |
| 3 编译 + VM 基础 | T010-T011 | `print 1+2` → 3；fixtures 起步 |
| 4 对象系统 | T012-T017 | 容器 fixtures 全过 |
| 5 函数/类 | T018-T022 | 06 用例全过 |
| 6 异常/模块 | T023-T025 | 07/09 用例全过；缓存命中率 > 90% |
| 7 GC | T026-T027 | GC 压力单测（含 ASan） |
| 8 调度器 | T028-T030 | 08 用例 + 100 goroutine 压测 |
| 9 async | T031-T032 | IO echo server fixture |
| 10 C API + Tier1 | T033-T036 | 嵌入示例 + stdlib 单测 |
| 11 工具链 | T037-T040 | fmt 幂等；debug 断点用例 |
| 12 发布 | T041-T044 | 基准门禁建立 + v0.1 |

## 任务文档模板约定

每个任务文档固定包含：头部元信息（阶段/依赖/规模/状态/设计文档引用）、目标与范围（含明确不做）、实现要点（要点 + 关键接口签名）、涉及文件、验收标准（DoD 可勾选清单）、测试计划（C 单测用例表 + ms 脚本用例及 golden 期望）、风险与备注。

早期任务（P1-P3）的"ms 用例"以 token 流 / AST 快照形式给出；T011 起为可运行 `.ms` + 期望输出。所有 `.ms` 用例落盘路径写明 `tests/fixtures/<topic>/`，conformance 用例落 `tests/conformance/`。
