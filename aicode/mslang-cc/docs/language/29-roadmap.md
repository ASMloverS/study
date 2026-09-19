# 29 项目结构与路线图

## 1. 仓库结构

```
mslang-cc/
  CMakeLists.txt            顶层构建（C11, -Wall -Wextra -Werror）
  include/ms.h              公共 C API（稳定 ABI）
  cmake/                    工具链脚本、测试注册
  src/
    common/                 类型别名/平台宏/工具宏
    scanner/  parser/  binder/  compiler/
    vm/                     dispatch 循环、frames、builtins 桥
    obj/                    对象系统（22）
    gc/                     分代 + 并发标记（23）
    sched/  reactor/        调度器与平台 IO（24）
    module/                 加载与 .msc 缓存（09）
    ms_api/                 C API 实现（25）
    stdlib/                 Tier1/2 模块（26）
    plat/                   线程/原子/IOCP-epoll-kqueue 薄封装
  tools/                    ms 主命令（27）
  tests/
    unit/                   C 单测
    fixtures/               .ms 端到端
    conformance/            金输出测试（含旧 mslang 迁移集）
  benchmarks/               基准（§5）
  examples/                 示例脚本（随版本可运行）
  docs/                     language/（本目录）等
  .clang-format  .editorconfig
```

## 2. 构建与 CI 矩阵

| 维度 | 值 |
|------|-----|
| 编译器 | MSVC 2022+ / GCC 10+ / Clang 12+ |
| 平台 | Windows 10+ / Ubuntu 20.04+ / macOS 11+ |
| 配置 | Debug / Release / ASan+UBSan（夜跑） |
| 门禁 | ctest 全绿、fmt --check、C 风格（clang-format --check）、基准回归 |

## 3. 实施阶段（12 期，每期有测试门）

| 期 | 内容 | 出口标准 |
|----|------|----------|
| 0 | 骨架：CMake/common/plat/CI/测试框架 | 三平台空壳构建绿 |
| 1 | Scanner + Token + 诊断 | 词法单测全过（01 文档用例全覆盖） |
| 2 | Parser → AST（全语法）+ Binder | 全部 conformance 语法树快照匹配 |
| 3 | Compiler + 栈 VM 基础（变量/运算/控制流） | `print 1+2` → 3；fixtures 起步 |
| 4 | 对象系统：string/list/map/tuple/set/bigint + 切片/插值 | 容器 fixtures 全过 |
| 5 | 函数/闭包/生成器 + class/继承/dunder | 06 用例全过 |
| 6 | 异常/defer + 模块系统/.msc 缓存 | 07/09 用例全过；缓存命中率 > 90%（二次运行） |
| 7 | GC 完整版（分代 + 并发标记 + 弱引用） | GC 压力单测（含 ASan）24h 随机脚本无崩溃 |
| 8 | 多线程调度器 + channel + select + sync | 08 用例 + 100 goroutine 压测 |
| 9 | async/await + netpoller + timers | IO echo server fixture（千并发连接） |
| 10 | C API + 扩展加载 + Tier1 标准库 | 嵌入示例（25 §9）+ stdlib 单测 |
| 11 | 工具链：run/repl/fmt/dump/debug | fmt 幂等测试；debug 断点用例 |
| 12 | test/build/pkg/doc + Tier2 + 优化（quickening 评估） | 基准门禁建立 + v0.1 发布 |

## 4. 成功标准（v0.1）

- conformance ≥ 300 用例全绿；examples 全可运行。
- fib(32) 性能：不低于旧 mslang-c 的 80%（寄存器→栈式换代的合理折让）；目标追 CPython 3.12 的 1.5×。
- GC：minor P99 < 1ms；major STW 总量 P99 < 500µs（基准报告）。
- 千并发 echo（net + 调度器）稳定 10 分钟。

## 5. 基准门禁

- 用例（benchmarks/）：fib、字符串拼接、map 读写、对象字段访问（IC）、channel ping-pong、goroutine spawn 风暴、GC 压力（分配速率扫描）、JSON 序列化。
- 基线：`baseline.json`（Release，9 次中位）；阈值：wall-clock ±5%、指令数 ±0.5%、GC 暂停分布回归即失败。
- 触发：每 PR 跑核心 5 例；每周全量。

## 6. 风险登记

| 风险 | 缓解 |
|------|------|
| 并发 GC × 多 mutator 正确性 | 阶段 7 独立压测（随机脚本 + ASan）；先单 mutator 后逐加 |
| 栈式 VM 性能不达预期 | quickening 与 IC 兜底；阶段 12 评估；基准先行 |
| Windows IOCP 与 epoll 语义差异 | reactor 抽象 + 三平台 CI fixture 同源 |
| channel/select 公平性与性能 | 有锁实现先行，无锁优化后置（基准驱动） |
| 栈溢出 × 可增长栈迁移 | 阶段 8 专项模糊测试（深递归 + 闭包逃逸） |

## 7. 版本策略

- `v0.x`：语言特性冻结每期递增；ABI 不承诺。
- `v1.0`：语言 + ABI 冻结；此后破坏性变更须 major。
- 语义版本三段 + 编译器版本独立（`.msc` 指纹组成部分）。
