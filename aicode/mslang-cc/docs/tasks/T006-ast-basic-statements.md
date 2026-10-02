# T006 — AST 定义与声明/基础语句解析

- 阶段: P2 语法（roadmap 期 2）
- 依赖: T005
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/04-statements.md` §1-§3 §10、`03-expressions.md`（AST 节点预留）、`20-architecture.md` §1

## 1. 目标与范围

定义全语言 AST 节点集（本任务落盘声明与基础语句所需节点，其余节点类型一并定义、解析后续任务填入），实现递归下降 Parser 骨架与以下语法：`var`/`const`/`:=` 声明、赋值族（普通/复合/索引/成员/解构）、表达式语句、`if/else if/else`、块作用域。

不做：for/switch/try/defer/go/select/import/func/class 的解析（T008）与表达式解析主体（T007，本任务仅接入字面量与标识符最小集以便语句自测）。

## 2. 实现要点

- AST 采用区段分配（arena）一次性释放；节点带 `line/col` 供诊断与行号表。

```c
typedef enum AstKind {
  AST_MODULE, AST_BLOCK,
  AST_VAR_DECL,        // var x = e / var x / const / := （flag 区分）
  AST_ASSIGN,          // 目标: 标识符/索引/成员/元组解构
  AST_DESTRUCTURE,     // 含 *rest（至多一个，可任意位置）
  AST_EXPR_STMT, AST_IF, AST_IDENT, AST_NIL, AST_BOOL,
  AST_INT, AST_FLOAT, AST_BIGINT, AST_STRING,  // 字符串含插值段列表
  /* T007/T008 扩展: 二元/一元/三元/调用/索引/切片/成员/推导式/… */
} AstKind;

typedef struct AstNode {
  AstKind kind; int32_t line, col;
  union { /* 各节点载荷；字符串段、解构目标数组用区段内指针 */ } as;
} AstNode;
```

- Parser：逐 token 拉取（scanner 按需供给），错误恢复 = 跳至下一个 `;`/`}` 边界后继续，支撑单次编译 ≤20 条诊断。
- 声明规则（04 §1）：`var` 可无初值（默认 nil）；`:=` 仅函数/块作用域（模块顶层非法 → SyntaxError MSE1xxx）；同作用域重复声明（`var x` 后 `x :=`）报错；不同作用域遮蔽合法。
- 赋值目标合法性检查：表达式语句可省值；`x += 1` 等复合赋值展开目标为读写（AST 保留复合形态，编译期展开）。
- `if` 条件加括号是 SyntaxError（强制 Go 风格）；块必须花括号（单语句也不例外）。
- 表达式语句：任意表达式 + ASI 换行即语句；裸 `;` 在源码中非法（ASI 产物除外——parser 直接消费不报错）。

## 3. 涉及文件

新增: `src/parser/ast.h`、`src/parser/parser.h`、`src/parser/parser.c`（声明/基础语句部分）、`tests/unit/test_parser_basic.c`
修改: `CMakeLists.txt`（parser 模块）

## 4. 验收标准（DoD）

- [ ] 04 §1-§3 示例全部解析成功且 AST 形态正确
- [ ] 非法输入（顶层 `:=`、`if (x) {}`、同作用域重复声明、解构双星号）各发 MSE1xxx
- [ ] 错误恢复：多错误文件一次编译收集多条诊断
- [ ] AST arena 用量统计接口可用（后续快照测试辅助）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_parser_basic.c`

| 用例 | 输入（摘要） | 期望 AST（摘要） |
|------|------|------|
| var 无初值 | `var y` | VAR_DECL(name=y, init=nil) |
| const | `const MAX = 100` | VAR_DECL(flag=const) |
| 短声明 | `x := 20` | VAR_DECL(flag=short) |
| 复合赋值 | `x += 1` | ASSIGN(op=+, target=IDENT) |
| 索引/成员赋值 | `xs[0] = v` / `obj.f = v` | ASSIGN(target=INDEX/MEMBER) |
| 解构 | `a, b = b, a` | ASSIGN(target=TUPLE) |
| 星号收集 | `a, *mid, z = xs` | DESTRUCTURE(rest=mid) |
| if 链 | `if a {} else if b {} else {}` | IF(else=IF(...)) |
| 顶层 `:=` | `x := 1`（模块层） | MSE1xxx |
| 条件带括号 | `if (x) {}` | MSE1xxx |
| 重复声明 | `var x` 后 `x := 1` 同块 | MSE1xxx |

### 5.2 ms 用例

无独立运行用例（尚无执行器）；AST 快照 conformance 统一在 T009 建立。本任务先在单测内嵌源码片段断言 AST。

## 6. 风险与备注

- AST 节点全集（含 T007/T008 的枚举位）本任务一次定义到位，避免后续频繁改动 `ast.h` 引发快照基线漂移。
- 错误恢复策略保持简单（同步点：`;` `}`），不为追求恢复精度引入复杂状态机。
