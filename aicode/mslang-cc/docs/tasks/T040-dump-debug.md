# T040 — ms dump 完整化与 debug TUI

- 阶段: P11 工具链（roadmap 期 11）
- 依赖: T029
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/27-toolchain.md` §8 §9、`21-bytecode.md` §4 §5、`24-scheduler.md` §4

## 1. 目标与范围

完善 `ms dump`（chunk 清单/常量池/指令流/upvalue 描述/IC 槽数/`--raw` 十六进制/`--chunk=<name>` 选中），实现 `ms debug` TUI 调试器：断点表（文件:行）、单步 n/s/fin、`p expr` 当前帧求值、`bt` 栈回溯、`goroutines` 列表切换、`watch var`、单 worker 确定性模式。

不做：DAP 协议（27 §9 v2）、条件断点/日志点（v2 评估）。

## 2. 实现要点

- dump（27 §8）：反汇编信息完备化（upvalue 描述、IC 槽数、嵌套 chunk 树）——T010 雏形升级；`--raw` 输出 .msc 字节流十六进制（调试缓存）；`--chunk` 按名过滤（模块/函数名匹配）。
- debug 机制（27 §9）：VM debug hook——断点表按 文件:line 注册（行号表反查指令区间）；CHECK_SAFEPOINT 轮询命中（复用 T029 检查点：断点激活时预算缩为每指令检查）；全局停（多 G 程序冻结于断点，单 worker 模式运行——`MS_SCHED_WORKERS=1` 强制）。
- 断点：`b file:line` / `b func`（函数名→入口行）；命中挂起全部 G → 进入命令循环。
- 步进：`n` 下一行（行号表区间推进）/`s` 步入（CALL 前 hook）/`fin` 步出（帧计数归零）。
- `p expr`：当前挂起帧上下文求值（取该帧局部/全局/upvalue 环境，编译为临时 chunk 在帧上下文执行——只读约定，副作用自担文档化）。
- `bt`：帧链打印（chunk 名:行）；`goroutines`：G 列表与状态（goroutineStats 消费）+ 切换查看各 G 栈。
- `watch var`：断点命中时打印变量名值；变量名解析经 frame map 槽位反查。
- TUI 交互：终端行编辑（plat 层最小 readline：历史/编辑；无第三方依赖）；`--tui` 全屏模式（源码窗 + 命令行）为可选增强，v1 先命令行循环形态（27 §9 功能面达标即算）。

## 3. 涉及文件

新增: `src/vm/debug_hook.c/.h`、`tools/debug.c`（命令循环/TUI）、`tools/lineedit.c/.h`、`tests/unit/test_debug_hook.c`、`tests/fixtures/debug/*.ms`（配合 `.dbg` 脚本化命令序列）
修改: `src/compiler/disasm.c`（完整化）、`tools/ms.c`、`src/vm/vm.c`（hook 检查点）、`src/sched/sched.c`（全局停/单 worker 强制）

## 4. 验收标准（DoD）

- [ ] dump：函数 chunk 树 + upvalue 描述 + IC 槽数 + `--raw` 全过（含 21 §4 样式回归）
- [ ] debug：`b file:line` 命中停；`n/s/fin` 步进行为正确（行推进断言）
- [ ] `p expr` 在断点处输出局部/全局/捕获变量值
- [ ] `bt` 显示帧链（函数名 + 行）；`goroutines` 列出与切换
- [ ] `watch` 多次命中打印变化值
- [ ] 多 goroutine 程序断点全局停（其余 G 冻结，无输出穿插）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_debug_hook.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 断点注册 | file:line | 行号表区间映射正确 |
| 命中回调 | 执行过断点行 | hook 触发一次 |
| 单步行进 | n ×3 | pc 跨 3 个行区间 |
| 步入/步出 | CALL 前后 | 帧计数正确 |
| watch 解析 | 变量名 | 槽位反查命中 |

### 5.2 fixtures `tests/fixtures/debug/`

- `fib_debug.ms` + `fib.dbg`（命令序列）：

```text
b fib_debug.ms:5
run
bt
p n
n
p n
c
```

期望输出（golden）：两次 `p n` 值随断点/步进变化。

- `multi_g.ms` + `multi_g.dbg`：双 go 程序断点全局停验证
- `dump_full.ms`：dump 三种模式输出快照（含 --chunk 过滤）

## 6. 风险与备注

- 脚本化调试会话（.dbg 序列）是可测试性关键：debug 命令循环支持 `--script file` 管道回放（同 REPL 管道模式基础设施复用 T037）。
- `p expr` 的帧上下文求值实现走"受控求值 chunk"（禁止赋值目标编译），降低误改状态风险。
