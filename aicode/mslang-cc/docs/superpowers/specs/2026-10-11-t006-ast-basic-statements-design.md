# T006 AST 与基础语句 — 设计决策文档

> 本文档记录 `docs/tasks/T006-ast-basic-statements.md` 规格未明确、经用户批准的实现决策。
> 规格主体（节点集、语法范围、DoD）以任务文档与 `docs/language/04-statements.md` 为准。

## 1. 表达式最小集边界

DoD 要求 04 §1-§3 示例全部解析成功，其中 §3 的 `if x > 0` / `else if x == 0` 需要比较运算。

- 纳入本任务：字面量全集（int/float/bigint/string 含插值段列表/rune/bool/nil）、标识符、
  后缀 `[e]` 索引与 `.name` 成员访问（赋值目标 `xs[0]` / `obj.f` 需要）、单级比较运算
  （`== != < <= > >=`，比较不可链式，链式报 MSE1007）。
- 不纳入（T007）：算术/移位/位运算、逻辑短路、三元、调用、切片、容器字面量、推导式、
  `<-ch` 接收、await、go 表达式、一元运算符、括号分组（`(` 在 primary 位置报意外 token）。

## 2. Parser 内轻量作用域栈

仅服务两个语法检查（04 §1）：同作用域重复声明（MSE1004）、模块顶层 `:=`（MSE1002）。
完整作用域/名称解析/遮蔽语义归 T009 Binder。实现：malloc 链式栈，块 `{` push `}` pop，
名称借用源指针 (start,len) 比较；模块作用域 = 最外层（depth 0）。

## 3. 语法错误码 MSE1000-1008

| 码 | 含义 |
|----|------|
| 1000 | 意外 token（通用，含"expected expression"） |
| 1001 | 缺少期望的语法成分（消息指明，如 `'{'`） |
| 1002 | `:=` 出现在模块顶层 |
| 1003 | if 条件加了括号（Go 风格强制） |
| 1004 | 同作用域重复声明 |
| 1005 | 解构目标 `*` 多于一个 |
| 1006 | 非法赋值目标 |
| 1007 | 比较运算符链式 |
| 1008 | `*` 目标未跟赋值 |

## 4. `:=` 仅单目标

`a, b := f()` 设计文档未定义 → 不支持（报 1006/1000）；元组与解构赋值一律 `=`。
任务测试表中短声明用例包在块内（`{ x := 20 }`），因模块顶层 `:=` 本身非法（1002）。

## 5. 分号一律静默消费

Parser 无法区分 ASI 合成分号与源码分号（token 无标记），语句终止符 `;` 一律消费不报错
（任务文档："parser 直接消费不报错"）。语句列表循环跳过孤立 `;`。语句后既非 `;` 也非
`}`/EOF 且未换行 → MSE1001。

## 6. 错误恢复策略

- 语句级 panic 模式：首错后屏蔽级联诊断；同步点 = 下一个 `;`（消费）或 `}`（停留）或 EOF。
- if 条件括号（1003）就地恢复：报诊断后消费 `(`，条件后配对消费 `)`，不进 panic。
- 扫描器已内部恢复的词法错误（如未闭合串）由 scanner 报 MSE0xxx，parser 正常消费产物 token。

## 7. 文件布局与 CMake

- `src/parser/ast.h`：AstKind 枚举一次定义到位（含 T007/T008 扩展位，避免快照基线漂移）、
  AstNode（kind + line/col + union 载荷）、AstArena 区段分配器 API 与用量/节点计数统计。
- `src/parser/parser.h`：Parser API（内嵌 Scanner）+ MseSyntaxCode。
- `src/parser/parser.c`：arena 实现 + 递归下降解析（覆盖原占位单元）；ms_parser 链接 ms_scanner。
- `tests/unit/test_parser_basic.c`：单测（内嵌源码片段断言 AST）。
- 构建改动仅两处：根 CMakeLists 的 ms_parser 链接、tests/CMakeLists.txt 注册 parser_basic。

## 8. 关键数据形态

- VAR_DECL：flags 位域（SHORT=1, CONST=2），init==NULL 即无初值（默认 nil）。
- ASSIGN：target（IDENT/INDEX/MEMBER/TUPLE）+ value + op（AST_OP_NONE 为普通赋值，复合保留形态）。
- DESTRUCTURE：targets 数组 + restIndex（star 位置，-1 无；DESTRUCTURE 节点必有一个）+ value。
- IF：cond + thenBlock + elseNode（else-if 链为嵌套 AST_IF；else 块为 AST_BLOCK）。
- STRING：段列表（expr!=NULL 为插值段，否则字面段借用源文本，转义原文保留）；raw 串单段。
- BIGINT：借用源文本，显式 `n` 后缀已剥离。
- 位置：节点取构造起始 token 的 line/col（1-based，col 按 rune 计）。
