# T037 — ms run 完整化与 REPL

- 阶段: P11 工具链（roadmap 期 11）
- 依赖: T024
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/27-toolchain.md` §1 §2 §10

## 1. 目标与范围

完善 `ms run`/直接 `ms app.ms` 执行路径（args 传 `os.args`、退出码体系全接通）与 REPL（多行续行、`:` 命令、自动打印、`import *` 白名单）。

不做：`--watch` 等未列特性。

## 2. 实现要点

- run（27 §1）：`ms run app.ms [args...]` 与 `ms app.ms` 等价；args 注入 os.args（argv[0] = 脚本路径）；`--` 分隔符后全归 args。
- 退出码（27 §10）：0 成功 / 1 未捕获异常 / 2 死锁 / 3 编译错误 / 4 用法错误 / 5 内部错误——统一 CLI 层收口（各子系统错误码归类映射复核）。
- REPL（27 §2）：逐行读→编译→运行→自动打印表达式结果（repr 形态）；括号/花括号未闭合自动续行（以 scanner parenDepth + token 流尾态判定）；`:q` 退出；命令：`:load f.ms`、`:type x`、`:dis fn`（复用 dump）、`:gc`（强制 full + 统计）、`:stats`（VM 指标）、`:time expr`（计时执行）。
- REPL 环境：行间状态保持（全局命名空间连续）；`import *` 仅 REPL 允许（fmt/linter 拒——T038 侧；REPL 路径放行）。
- 多行输入的 ASI：REPL 以"完整语句/表达式"为提交单位（续行期间不触发执行）。
- Windows 控制台 UTF-8 输出模式设置（chcp/VT）。

## 3. 涉及文件

新增: `tools/repl.c`、`tools/cli_common.c/.h`（参数解析/退出码映射）、`tests/fixtures/repl_script/*.in`（管道喂入式 REPL 会话用例）
修改: `tools/ms.c`（子命令分派、run 路径）、`src/stdlib/os.c`（args 注入点复核）、`tests/` 跑器（REPL 会话模式：stdin 喂 `.in`，比对 stdout）

## 4. 验收标准（DoD）

- [ ] `ms app.ms a b -- -x` → os.args == [脚本路径, "a", "b", "-x"]
- [ ] 五类退出码各有端到端用例（.code 断言）
- [ ] REPL：单行/多行/未闭合续行/自动打印全过
- [ ] `:` 六命令全部可用（:load/:type/:dis/:gc/:stats/:time）
- [ ] REPL 跨行全局状态保持；`import *` 可用
- [ ] 管道模式 `echo '1+2' | ms repl` 输出 `3`

## 5. 测试计划

### 5.1 C 单测

REPL 主体经会话 fixtures 覆盖；CLI 解析单测 `tests/unit/test_cli_args.c`（参数分隔与归类）。

### 5.2 会话 fixtures `tests/fixtures/repl_script/`

- `basic.in`：

```text
1 + 2
var x = 10
x * 2
func add(a, b) { return a + b }
add(x,
    5)
:q
```

期望输出：`3`、`20`、`15`（自动打印仅表达式行；赋值行无输出）。

- `commands.in`：`:type 1`→`int`、`:dis` 函数、`:gc` 统计行、`:time 1+1` 耗时行（模式匹配）
- `multilane.in`：`{` 起多行块 + 字符串跨行
- `run_args.ms` + 参数（跑器参数注入）：os.args 输出断言

## 6. 风险与备注

- REPL 自动打印的判定：行输入是"表达式"而非"语句"时打印（解析尝试顺序：语句优先、失败回退表达式求值）——空白/注释行无输出。
- REPL 每行独立编译但共享 VM/全局——.msc 缓存对 REPL 关闭（无源文件指纹意义）。
