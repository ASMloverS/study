# T007 — 表达式解析：优先级与后缀链

- 阶段: P2 语法（roadmap 期 2）
- 依赖: T006
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/03-expressions.md` 全文、`05-functions.md` §2

## 1. 目标与范围

实现全语言表达式解析：14 级优先级（03 §1 表）、后缀链（调用/成员/索引/切片）、三元、逻辑短路、`and/or/not` 拼写、推导式三式、生成器表达式、函数表达式、插值字符串 AST、rune 字面量接入。

不做：`go`/`await`/`<-ch` 表达式与语句级 channel 语法（T008，因与语句边界耦合）。

## 2. 实现要点

- 优先级从低到高（03 §1）：`?:` → `||` → `&&` → 比较（`== != < <= > >= is in`，不可链式）→ `|` → `^` → `&` → `<< >>` → `+ -` → `* / %` → `**`（右结合）→ 一元 `! - ~` → 前缀表达式 → 后缀链。同级左结合（赋值除外、三元右结合）。
- 递归下降分层函数：`parseExpr` → `parseTernary` → `parseOr` → `parseAnd` → `parseCompare` → … → `parseUnary` → `parsePostfix` → `parsePrimary`。
- 链式比较 `a < b < c` 非法：`parseCompare` 只接受一个比较操作数，多出一个发 MSE1xxx。
- 后缀链（左结合）：`f(1, *xs)`（星号展开实参）、`obj.method(args)`、`xs[i]`、`xs[a:b:c]`（端点可省略、步进可负；切片是独立 AST_SLICE 后缀，不是索引特例）。
- 推导式（03 §2）：`[e for x in xs if c]` / `{k: v for …}` / `{e for …}`；多 `for` 嵌套、多 `if` 合取；与空容器字面量歧义在 `{` 后前瞻区分（`{}` 为空 map；`{x` 后跟 `:` 为 map 字面量、`for` 关键字为推导式、否则 set 字面量/推导）。
- 生成器表达式 `(x * x for x in xs if x > 0)`：仅允许作调用实参（含 `f(g for g in xs)` 单参形态）或赋值右值（带括号），其余位置发 MSE1xxx。
- 函数表达式 `func(a, b) { … }`：一等值（05 §2）。
- 插值字符串 AST：常量段与表达式段交错列表（T005 token 序列 → AST_STRING_INTERP）。
- `is` 不可重载、`in` 走 `__contains__` —— 仅语义注释，解析为普通二元节点。
- 实参求值顺序从左到右：AST 按序存放即可（编译期保证）。

## 3. 涉及文件

新增: `tests/unit/test_parser_expr.c`
修改: `src/parser/parser.c`（表达式分层）

## 4. 验收标准（DoD）

- [ ] 03 §1 优先级表逐行有用例（含结合性与右结合 `**`、三元右结合）
- [ ] `a < b < c`、裸生成器表达式错位、`f(1, *xs, 2)` 后续位置实参等非法形态报 MSE1xxx
- [ ] 推导式三式与多 for/if 嵌套解析正确
- [ ] 切片四种省略形态（`[a:]` `[:b]` `[::c]` `[a:b:c]`）AST 正确
- [ ] 插值嵌套字符串 AST 与段序正确

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_parser_expr.c`

| 用例 | 输入 | 期望（摘要） |
|------|------|------|
| 优先级 | `1 + 2 * 3` | ADD(1, MUL(2,3)) |
| 幂右结合 | `2 ** 3 ** 2` | POW(2, POW(3,2)) |
| 逻辑拼写 | `a and b or not c` | OR(AND(a,b), NOT(c)) |
| 比较单次 | `a == b` OK；`a < b < c` | 后者 MSE1xxx |
| 位运算层级 | `a | b ^ c & d << 2` | 层级正确 |
| 后缀链 | `obj.m(x)[i].f{…}` 不合法形态排除；`obj.m(x)[i]` | POSTFIX 链序正确 |
| 切片 | `xs[a:b:c]` / `xs[::2]` | SLICE(start,end,step) 端点可 NIL |
| 调用展开 | `f(1, *xs)` | CALL(args, spread=[1]) |
| 推导式 | `[x*x for x in xs if x%2==0]` | LISTCOMP(expr=…, fors=[…], ifs=[…]) |
| 双 for | `[x+y for x in a for y in b]` | fors 长度 2 |
| map/set 歧义 | `{}` / `{1:2}` / `{1,2}` / `{k:v for …}` / `{x for …}` | 五种形态各自正确 |
| 生成器表达式 | `sum(x*x for x in xs)` | GENEXPR 作实参 |
| 函数表达式 | `func(x) { return x*2 }` | FUNC_EXPR |
| 插值 | `"a${b}c${d(e)}"` | 段序 [str,ident,str,call] |
| 三元 | `a ? b : c ? d : e` | 右结合嵌套 |

### 5.2 ms 用例

AST 快照 conformance 归 T009 统一落盘；本任务单测内嵌断言。

## 6. 风险与备注

- `{` 歧义前瞻最多需看 2 个 token（`{x for` / `{x:` / `{x,` / `{x}`），parser 保存/回溯点实现受限回溯（仅此处允许，其他保持单向推进）。
- 一元_minus 与 `-` ** 的交互：`-2 ** 2` 解析为 `-(2 ** 2)`（一元低于 `**`，与 Python 对齐，需用例锁定）。
