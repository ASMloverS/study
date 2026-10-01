# 20 总体架构

CPython 式管线，Go 式运行时（并发调度 + 并发 GC），纯 C11 单仓实现。

## 1. 编译管线

```
        .ms 源文件
            │ Scanner        ASI、插值切分、raw/rune 字面量、/// 文档注释标记
            ▼
         Token 流
            │ Parser         递归下降（非单遍 Pratt），产出完整 AST
            ▼
          AST
            │ Binder         作用域/名称解析（local/global/upvalue 分类）、
            ▼               常量折叠、诊断收集
        标注 AST
            │ Compiler       AST → 栈式字节码；peephole 优化；IC 槽分配
            ▼
      Chunk(s) + 常量池       ──序列化──▶ __mscache__/*.msc（指纹缓存）
            │
            ▼
         VM 执行（栈式 dispatch 循环）
```

- 前端（Scanner/Parser/Binder）产出 AST 同时供 REPL、`ms fmt`、`ms doc`、未来 LSP 复用。
- `.msc` = 序列化 chunk（magic + 编译器版本 + 源 FNV-1a 指纹 + 常量池 + 指令流），命中即跳过前端。
- 诊断：编译期错误统一 `MsDiagnostic { level, code, file, line, col, len, message }`；最多一次编译报 20 条；格式 `file:line:col: error[MSExxxx]: message` + 源码行 + `^` 指示。

## 2. 运行时组件图

```
┌──────────────────────────── ms 进程 ────────────────────────────┐
│  tools/ CLI（run/repl/fmt/test/…）                                │
│  ┌────────────────────────── ms_api.c ─────────────────────────┐ │
│  │  ms.h 公共 C API（嵌入方 / 扩展模块）                        │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌─ VM 核心 ──────────────────────────────────────────────────┐ │
│  │  栈式解释循环 · frames · 内联缓存 · handles 注册表          │ │
│  └───────┬────────────────────────────────────────────────────┘ │
│  ┌───────▼──────┐  ┌─ 调度器 sched ────────────────────────────┐ │
│  │ 模块加载 module│  │ worker×N（OS 线程）· runq + work stealing │ │
│  └──────────────┘  │ channel · timers · netpoller(epoll/IOCP)  │ │
│                    └───────┬───────────────────────────────────┘ │
│  ┌─ GC ────────────────────▼───────────────────────────────────┐ │
│  │ young 双半空间 + old 标记清除 · 并发标记线程 · 混合写屏障    │ │
│  └─────────────────────────────────────────────────────────────┘ │
│  ┌─ stdlib ────────────────────────────────────────────────────┐ │
│  │ Tier0 内置 + Tier1/2 模块（C 实现注册进 VM）                 │ │
│  └─────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

## 3. 线程模型

| 线程 | 数量 | 职责 |
|------|------|------|
| worker（M） | 默认 CPU 核数 | 执行 goroutine（G），跑 VM 解释循环 |
| GC 标记线程 | 1 | old 区并发三色标记 |
| netpoller | 1 | IO 就绪（epoll/kqueue/IOCP）→ 唤醒 G |
| blocking pool | 少量（≤4） | 阻塞式系统调用替身（Windows 文件 IO 等） |

- 主线程也是一个 worker；脚本层无感（`24-scheduler.md`）。

## 4. 源码目录 ↔ 组件

```
src/
  scanner/    token 化（01 词法）
  parser/     AST 构造（03/04 语法）
  binder/     名称解析/常量折叠
  compiler/   字节码生成 + peephole
  vm/         dispatch 循环、frames、builtins 桥
  obj/        对象系统：string/list/map/… Shape/IC（22）
  gc/         分代 + 并发标记（23）
  sched/      G/M 调度、channel、timers、netpoller（24）
  reactor/    平台 IO 多路复用封装
  module/     模块加载与缓存（09）
  ms_api/     C API 实现（25）
  stdlib/     Tier1/2 标准库模块（26）
tools/        ms 主命令与子命令（27）
```

## 5. 关键数据流（示例：`ms app.ms`）

1. CLI 解析 → `msNewVM` → 编译 app.ms（含 .msc 查询）。
2. `module` 加载主 chunk，创建主 G，投入调度器。
3. worker 取 G 执行；遇 import → 递归编译/缓存；遇 go/await → 调度交互。
4. GC 依分配节奏后台并发运行；safepoint 与协程抢占共用回边检查点。
5. 主 G 结束且无活跃后台 Future/goroutine（可配置等待策略）→ 进程退出。

## 6. 性能与可观测

- `MS_VM_STATS=1` 编译期开关：指令计数、GC 暂停分布、IC 命中率、调度器指标。
- `ms dump`：反汇编；`--trace`：逐步执行日志（教学/调试）。
- 基准门禁（`benchmarks/`）随 CI 运行，回归阈值见 `29-roadmap.md`。
- 优化路线（v1 之后）：quickening（热指令特化）、尾调用消除评估、NaN-boxing 打开验证。

## 7. 可移植性

- C11（`<stdatomic.h>`、`<threads.h>` 不依赖——自实现薄封装 `plat/`：线程/互斥/条件变量/IOCP-epoll 适配）。
- 编译器：MSVC 2022+ / GCC 10+ / Clang 12+；平台：Windows 10+ / Linux (glibc) / macOS 11+。
- dispatch：GCC/Clang computed goto；MSVC switch。字节码语义两者一致。
