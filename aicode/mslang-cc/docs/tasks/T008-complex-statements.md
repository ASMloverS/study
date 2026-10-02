# T008 — 复合语句与函数/类声明解析

- 阶段: P2 语法（roadmap 期 2）
- 依赖: T007
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/04-statements.md` §4-§9 §10、`05-functions.md` §1 §8、`06-classes.md` §1 §2 §4

## 1. 目标与范围

完成全部剩余语法解析：for 五形态、while、switch（值/类型）、break/continue/return、throw/try/catch/finally、defer、go 语句、channel 语句（send/recv/select）、import 三式、函数声明（默认参/rest/表达式体/func*/async）、class/trait 声明（继承清单/static/类字段）。完成后 03/04/05/06 全语法可解析。

不做：任何语义检查（作用域归 T009）与代码生成。

## 2. 实现要点

- for 形态判别（04 §4）：`for init; cond; post {}` 三段式（均可省略） / `for x in xs {}` 迭代 / `for k, v in xs {}` 解构迭代 / `for cond {}` / `for {}` 无限；`while cond {}` 独立节点。判别依据 `for` 后第 1/2 token（`;` → 三段式；`in` 前置分析 → 迭代；`{` → 无限；否则条件式）。
- switch 值式（04 §5.1）：`case 1, 2:` 多值、`default:`、隐式 break（无穿透标志）；类型式（04 §5.2）：`switch type(v)`、`case int, bigint:`、`case X as p:` 绑定进块作用域。
- try/catch（07 §1）：多个 catch 子句（`catch e` / `catch A, B { }` / `catch A as?? — 无，类型子句可省绑定`：`catch ValueError { }`）；类型子句在前、裸绑定兜底最后，违序发 MSE1xxx；finally 可选。
- defer：`defer f.close()` 注册调用表达式（04 §7），仅函数体内合法（模块层报错）。
- channel 语句（04 §8）：`ch <- v` send 语句、`v = <-ch` / `<-ch` 接收、`select { case v = <-ch1: … case ch2 <- x: … default: … }`。
- import（09 §2）：`import "m"` / `import "m" as x` / `from "m" import {a, b as c}` / `from "m" import *`；路径必须字符串字面量。
- 函数声明（05 §1 §8）：`func name(a, b = 1, *rest) {}` / `-> expr` 表达式体 / `func* name()` / `async func name()`；参数序：普通 → 默认 → 至多一个 `*rest`，违序报错。
- class/trait（06 §1 §2 §4）：`class Name : Base, Mix1, Mix2 { }`（基类清单）；类体成员：`var x, y` 字段声明、`func`（含 dunder）、`static func`、`static var`、`const VERSION = "1.0"`；类体内非方法位置 `this` 非法。
- 控制转移合法性留待 T009（作用域感知）：break/continue 仅循环内、return 顶层非法、await 位置约束（async/顶层）——本任务仅解析。

## 3. 涉及文件

新增: `tests/unit/test_parser_complex.c`
修改: `src/parser/parser.c`

## 4. 验收标准（DoD）

- [ ] 04/05/06 全部文档示例解析通过
- [ ] 五种 for 形态 + while 判别无歧义
- [ ] select/case 语法（recv 绑定/send/default）AST 正确
- [ ] catch 子句排序约束（类型在前裸绑定最后）报错正确
- [ ] `func*`/`async`/表达式体/默认参/rest 全排列用例通过
- [ ] class 继承清单与类体成员（static/var/const/func）解析正确

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_parser_complex.c`

| 用例 | 输入（摘要） | 期望（摘要） |
|------|------|------|
| 三段式 for | `for i := 0; i < 10; i++ {}` | FOR_TRI(init,cond,post) |
| 省略段 | `for ; ; {}` | FOR_TRI(全 NIL) |
| 迭代 for | `for x in xs {}` / `for k, v in m.items() {}` | FOR_IN(目标可解构) |
| 条件/无限 | `for cond {}` / `for {}` | FOR_WHILE / FOR_INF |
| 值 switch | `switch x { case 1, 2: … default: }` | SWITCH(cases 多值) |
| 类型 switch | `switch type(v) { case string as s: }` | TYPE_SWITCH(as 绑定) |
| catch 排序 | `try{}catch e{}catch V{}` | MSE1xxx（裸在前） |
| defer | `defer f.close()` | DEFER(call) |
| select | `select { case v = <-ch: … case ch2 <- x: … default: }` | SELECT 三类分支 |
| import | `from "m" import {a, b as c}` | IMPORT_FROM(names) |
| 函数全形态 | `func f(a, b=1, *r) -> a+b` | FUNC(defaults, rest, exprBody) |
| async/func* | `async func f()` / `func* g()` | flags 正确 |
| class | `class I : B, M { var x\n func m() {}\n static var t = 0 }` | CLASS(bases=[B,M], members) |
| 类内 this | `class A { var t = this.x }` | MSE1xxx |

### 5.2 ms 用例

统一由 T009 建立 AST 快照 conformance（`03-expr-*`、`04-stmt-*`、`05-func-*`、`06-class-*` 前缀，取自各文档示例）。

## 6. 风险与备注

- `for` 判别需小心 `for x in xs` 中 `in` 是关键字而非标识符；`for` 后跟 `{` 时为无限循环，与块语句区分。
- `switch type(v)` 中 `type` 是普通标识符（01 §2），解析器特判 `switch` 后首个 token 为 ID `type` 且随之为 `(`。
- `ch <- v` 语句与 `<-ch` 表达式的边界：赋值右值/表达式位置走 T007 前缀接收，语句位置首个 token 为 ID 且次 token `<-` 时按 send 语句解析。
