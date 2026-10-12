# T007 表达式解析 — 设计文档

- 日期: 2026-10-13
- 任务: docs/tasks/T007-expression-parsing.md（P2, 依赖 T006, 规模 L）
- 分支: feat/t007-expression-parsing

## 1. 目标

实现全语言表达式解析：14 级优先级阶梯、后缀链（调用/成员/索引/切片）、三元、逻辑短路、`and/or/not` 拼写、推导式三式、生成器表达式、函数表达式、插值字符串 AST、rune 字面量接入。不做 `go`/`await`/`<-ch`（T008）。

## 2. 方案选型

- 递归下降分层函数（任务文档明确列出层名），否决优先级爬升表驱动与拆分 parser_expr.c。
- 单文件 `src/parser/parser.c` 扩展，`src/parser/ast.h` union 补载荷（枚举位 T006 已全部预留）。

## 3. 已批准的偏差

| 偏差 | 任务文档建议 | 实际设计 | 理由 |
|------|--------------|----------|------|
| A | `{` 歧义处实现 parser 保存/回溯点（受限回溯） | 不实现回溯：`(`/`[`/`{` 先完整解析第一个子表达式，再看下一 token（`for`/`:`/`,`/闭括号）定形 | `for`、`:` 均终止表达式，回溯无必要；行为不变 |
| B | 函数表达式 `func(a, b) { … }` | 仅普通参数；默认参数、`*rest`、`->` 表达式体归 T008；补最小 `return` 语句解析（测试用例 `func(x) { return x*2 }` 需要） | 参数结构体设计为 T008 可复用 |

## 4. 详细设计

### 4.1 优先级阶梯

```
parseExpr → parseTernary → parseOr → parseAnd → parseCompare → parseBitOr
→ parseBitXor → parseBitAnd → parseShift → parseAdd → parseMul
→ parsePow → parseUnary → parsePostfix → parsePrimary
```

- 同级左结合；三元右结合（分支递归 parseTernary）；`**` 右结合（rhs = parseUnary）。
- `-2 ** 2` 解析为 `-(2 ** 2)`：一元层在 `**` 之下（rhs 吸收一元，`2 ** -3` 合法），Python 对齐，测试锁定。
- `parseCompare` 单次：多比较操作数发 `MSE_SYN_CHAINED_CMP`(1007，已有)。比较集 `== != < <= > >= is in`。
- 逻辑 `&& || and or` → AST_LOGIC（op 复用 AstBinOp 的 AND/OR）；`not` 一元（AST_UNARY，返回 bool 语义归语义层）。
- AstBinOp 补 `AST_OP_IS`、`AST_OP_IN`（解析为普通二元节点，`is` 不可重载、`in` 走 `__contains__` 仅注释）。

### 4.2 AST 载荷（ast.h union 新增）

- `unary`: `{ AstNode* operand; AstUnaryOp op; }`（NEG/NOT/INV）
- `logic`: `{ AstNode* lhs, * rhs; AstBinOp op; }`（AND/OR）
- `ternary`: `{ cond, thenExpr, elseExpr }`
- `call`: `{ AstNode* callee; AstNode** args; int32_t count; ms_u32 spread; }`（spread 为位图，位 i 表示 args[i] 为 `*` 展开）
- `slice`: `{ obj, start, end, step }`（端点可 NIL；独立 AST_SLICE，非索引特例）
- `list`/`set`: `{ items, count }`；`map`: `{ keys, values, count }`
- `compr`: LIST/MAP/SET 推导式与 GEN_EXPR 共用：`{ ms_u8 kind; AstNode* key; AstNode* value; ComprFor* fors; int32_t forCount; AstNode** conds; int32_t condCount; }`，其中 `ComprFor { AstNode* target; AstNode* iter; }`（target 为 IDENT 或 AST_TUPLE）；list/set 用 value，map 用 key:value
- `funcExpr`: `{ AstParam* params; int32_t paramCount; AstNode* body; }`，`AstParam { const char* name; int32_t nameLen; AstNode* defaultExpr; bool rest; }`（T007 仅填充 name；defaultExpr/rest 位 T008 复用）

### 4.3 消歧与 primary 扩展

- `(`：`(e)` 分组；`(e, …)`/`(e,)`/`()` tuple；`(e for …)` GEN_EXPR。
- `[`：`[e, …]` list；`[e for …]` LISTCOMP（多 for 嵌套、多 if 合取）。
- `{`：`{}` 空 map；`{k: v, …}` map；`{k: v for …}` MAPCOMP；`{e, …}`/`{e}` set；`{e for …}` SETCOMP。
- 推导式：`for` 目标按逗号列表解析（IDENT 或元组），`if` 条件列表，直至闭括号。
- 后缀链左结合：`f(args)`、`.name`、`[i]`、`[a:b:c]`（切片端点/步进可省略）。

### 4.4 生成器表达式位置限制

- GEN_EXPR 仅允许：调用唯一实参（`f(g for g in xs)`）或赋值右值（带括号，`(e for …)` 形态本身保证）。
- 检查点：二元构造助手（mkBinary 一处覆盖全部二元层）、一元、三元、后缀基座（GEN_EXPR 后跟 `(`/`[`/`.`）、插值段——直接操作数位置拒绝，发 `MSE_SYN_GENEXPR_POS`(1009)。
- 调用实参内：GEN_EXPR 后跟逗号 → 同码（须加括号）。
- `*xs` 展开后不允许后续实参 → `MSE_SYN_SPREAD_NOT_LAST`(1010)。

### 4.5 函数表达式与 return

- `func(params) { block }`：params 仅 `ID (, ID)*`；body 复用 parseBlock（开作用域）。
- 最小 `return`：`return [exprList]` + endSimpleStmt → AST_RETURN（载荷 `{ AstNode* value; }`，复用 exprStmt 布局）。

### 4.6 新增错误码

`MSE_SYN_GENEXPR_POS = 1009`、`MSE_SYN_SPREAD_NOT_LAST = 1010`（parser.h 追加）。

## 5. 已知不设检查的边角

- 元组右值内嵌带括号 genexpr（`x = (g for g in xs), 1`）：解析通过，T009 binder 收紧。无测试要求。

## 6. 测试计划

`tests/unit/test_parser_expr.c`（注册 tests/CMakeLists.txt，链 ms_parser），覆盖任务 §5.1 全表 15 例 + DoD 追加：

- 优先级/结合性：`1+2*3`、`2**3**2`、`a and b or not c`、`a|b^c&d<<2`、`-2**2`、三元右结合
- 非法形态：`a<b<c`(1007)、裸 genexpr 错位(1009)、`f(1,*xs,2)`(1010)
- 推导式三式 + 多 for/if + map/set 歧义五形态
- 切片四省略形态、调用展开 spread 位图、函数表达式、插值段序 `[str,ident,str,call]`

## 7. 验收（DoD 对照）

任务 §4 五项全部满足；ctest 全绿；clang-format 保持 28-style-c.md。
