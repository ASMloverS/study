# 27 工具链

单二进制 `ms`，子命令风格（`ms <cmd>`）。

## 1. 命令总表

| 命令 | 形式 | 说明 |
|------|------|------|
| run | `ms run app.ms [args...]` / `ms app.ms` | 运行脚本；args 传入 `os.args` |
| repl | `ms repl`（无参 `ms` 同效） | 交互式 REPL |
| fmt | `ms fmt <files|dirs>` | 格式化（唯一风格）；`--check` 仅校验（CI 用，非零退出） |
| test | `ms test [dirs]` | 发现并运行测试 |
| build | `ms build app.ms -o dist/` | 编译缓存 + 打包 |
| pkg | `ms pkg install/update/list` | 依赖管理 |
| doc | `ms doc [module|symbol]` | 文档查看/生成 |
| dump | `ms dump app.ms` | 字节码反汇编（`--chunk=<name>` 选中） |
| debug | `ms debug app.ms` | TUI 调试器 |
| version | `ms version` | 版本与构建信息 |

## 2. repl

- 多行输入（括号/花括号未闭合自动续行）；`:q` 退出。
- 命令（`:` 前缀）：`:load f.ms`、`:type x`、`:dis fn`（反汇编）、`:gc`、`:stats`、`:time expr`。
- 自动打印表达式结果（repr 形态）；`import *` 仅 REPL 允许。

## 3. fmt（唯一风格）

- 执行 `11-style-ms.md` §2 全部规则：4 空格、无分号、行宽 100（超限尽力断行）、运算符归一（`and→&&` 等）、import 分组排序。
- 幂等：fmt(fmt(x)) == fmt(x)；无法安全断行的长行保留并在 stderr 告警。
- `--write`（默认写回）/ `--check` / `--diff`。

## 4. test

- 用例文件：`*_test.ms`（Go 惯例）；用例函数：`func testXxx(t)`（`t` 为 testing 句柄）。

```ms
// strutil_test.ms
import "testing"
import "./strutil"

func testJoin(t) {
    t.expect(strutil.join(["a","b"], "-")).toBe("a-b")
}
func benchJoin(t) {
    t.bench(func() { strutil.join(range(1000).toList(), ",") })
}
```

- 运行：并行度 = CPU 核（每用例独立 VM，隔离崩溃）；输出：进度点 + 摘要 + 失败详单（文件:行 + diff）；支持 `--filter=Xxx -v --json`（CI）。
- 退出码：0 全过 / 1 失败 / 2 基准回归超阈值。

## 5. build

- `ms build app.ms -o dist/`：
  - `dist/__mscache__/*.msc`：入口及全依赖编译缓存；
  - `dist/app.msb`：单文件 bundle（zip：`manifest.json` + 入口 + 依赖源码 + 可选 .msc）。
- `ms run app.msb` 直接运行 bundle；bundle 内相对 import 重定向（部署单文件）。
- 不做原生可执行档打包（v2 评估 self-contained runner）。

## 6. pkg（v1 无中央 registry）

- 清单 `ms.pkg`（仓库根，TOML 子集）：

```toml
name = "myapp"
version = "0.1.0"

[deps]
"strings-plus" = { git = "https://github.com/u/strings-plus", ref = "v0.2.1" }
"local-util"   = { path = "../local-util" }
```

- `ms pkg install`：拉取至 `ms_packages/<name>@<ref>/`；生成/更新 `ms.lock`（精确 ref + 内容 hash）。
- 模块解析在 MS_PATH 之后查 `ms_packages/`（`09-modules.md` §4）。
- 无传递 semver 求解（ref 固定）；升级显式 `ms pkg update <name>`。

## 7. doc

- `ms doc strutil`：终端分页显示模块文档（抽取 `///` 文档注释）。
- `ms doc --html -o docs/`：生成静态 HTML（索引 + 每模块页，godoc 式简洁版式）。
- 顶层连续 `///` 行紧随其后（无空行）的声明才会被抽取。

## 8. dump

- 输出 chunk 清单：常量池、指令流（`21-bytecode.md` §4 样式）、upvalue 描述、IC 槽数。
- `--raw` 十六进制字节流（.msc 调试）。

## 9. debug（TUI 调试器）

- 机制：VM debug hook（断点表按 文件:行 注册，CHECK_SAFEPOINT 处轮询命中）。
- 功能：`b file:line` 断点、`n/s/fin` 单步/步入/步出、`p expr` 求值（当前帧上下文）、`bt` 栈回溯、`goroutines` 列表与切换、`watch var`。
- 单 worker 模式运行被调试程序（确定性），多 goroutine 程序仍可运行但调度冻结在断点全局停。
- v1 不做 DAP 协议（VS Code 集成 v2）。

## 10. 退出码约定

| 码 | 含义 |
|----|------|
| 0 | 成功 |
| 1 | 脚本未捕获异常 / 测试失败 |
| 2 | 死锁 / 基准回归 |
| 3 | 编译错误 / fmt --check 未过 |
| 4 | 用法错误（CLI 参数） |
| 5 | 内部错误（bug 报告提示） |
