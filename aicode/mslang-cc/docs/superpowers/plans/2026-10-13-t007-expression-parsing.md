# T007 表达式解析 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement full expression parsing for mslang: 14-level precedence ladder, postfix chains (call/member/index/slice), ternary, short-circuit logic, comprehensions, generator expressions, function expressions.

**Architecture:** Recursive-descent layered functions in `src/parser/parser.c` (single file, existing structure). AST payloads added to `src/parser/ast.h` union (enum slots already reserved by T006). No backtracking anywhere: `(`/`[`/`{` forms decided by the token following the first fully-parsed sub-expression.

**Tech Stack:** C11, CMake + MSVC (VS 2022 multi-config), custom test harness (`tests/unit/test_harness.h`), ctest.

**Spec:** `docs/superpowers/specs/2026-10-13-t007-expression-parsing-design.md`

---

## Context (read this first)

- Work dir: `C:\Workspace\Repositories\study\aicode\mslang-cc`. Branch: `feat/t007-expression-parsing` (already created).
- Git repo root is the PARENT directory (`C:/Workspace/Repositories/study`) — run all git commands from the mslang-cc workdir and stage only explicit mslang-cc paths.
- Build: `cmake --build build --config Debug` (VS 2022, multi-config; build output in `build/` only).
- Run one unit test: `.\build\tests\Debug\test_parser_expr.exe` (exit 0 = pass, prints `all tests passed`).
- Full suite: `ctest --test-dir build -C Debug --output-on-failure`.
- Test style: `TEST(name)` macro taking `TestCtx* ctx`, `EXPECT/EXPECT_EQ/EXPECT_F64_EQ` from `test_harness.h`. Statements are newline-separated; each test parses a small source into `AST_MODULE` and walks `mod->as.list.items[i]->as.exprStmt.expr`.
- Style: `docs/language/28-style-c.md` (lowerCamelCase functions, 2-space indent, Chinese comments). Match existing `src/parser/parser.c`.
- Commit format: `<gitmoji> <type>(<scope>): <message>` (e.g. `✨ feat(parser): ...`).
- AST payload reuses (documented deviations from spec §4.2, both cosmetic):
  - `AST_LOGIC` reuses `as.binary` (identical shape, short-circuit semantics distinct at compile stage).
  - `AST_LIST`/`AST_SET` reuse `as.list` `{items, count}` (same shape as MODULE/BLOCK).
  - `AST_RETURN` reuses `as.exprStmt.expr` as its value slot.
  - call spread is a parallel `bool*` array (arena), not a bitmap (no 32-arg limit).

## File Structure

- Modify: `src/parser/ast.h` — add `AstUnaryOp`, `AST_OP_IS/IN`, `AstComprFor`, `AstParam`, `AST_COMP_*` consts, union members `unary/ternary/call/slice/mapLit/compr/funcExpr`.
- Modify: `src/parser/parser.h` — add `MSE_SYN_GENEXPR_POS = 1009`, `MSE_SYN_SPREAD_NOT_LAST = 1010`.
- Modify: `src/parser/parser.c` — replace the minimal expression section (current lines 219–430: `parseExpr`, `parsePostfix`, `parsePrimary`, `parseExprList` area) with the full ladder; add `parseReturnStmt` + statement hook; add genexpr check in `parseStringLit`.
- Create: `tests/unit/test_parser_expr.c` — all T007 tests.
- Modify: `tests/CMakeLists.txt` — register `ms_add_test(parser_expr ms_parser)`.
- Modify (final task): `docs/tasks/T007-expression-parsing.md` + `docs/tasks/README.md` status.

---

### Task 1: Scaffolding + binary ladder (arithmetic/bitwise/pow/unary/compare)

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.h`
- Create: `tests/unit/test_parser_expr.c`
- Modify: `tests/CMakeLists.txt`, `src/parser/parser.c`

- [ ] **Step 1: Add AST payloads and error codes**

In `src/parser/ast.h`, extend `AstBinOp` (after `AST_OP_GE`, before `AST_OP_AND`):

```c
  AST_OP_IS,   // 同一性（03 §5，不可重载）
  AST_OP_IN,   // 成员包含（03 §5，走 __contains__）
```

Add after the `AstBinOp` enum:

```c
// 一元运算符（03 优先级级 12）
typedef enum AstUnaryOp {
  AST_UNOP_NEG = 1,  // -
  AST_UNOP_NOT,      // ! / not
  AST_UNOP_INV,      // ~
} AstUnaryOp;

// 推导式/生成器 for 子句：target 为 AST_IDENT 或 AST_TUPLE（多变量）
typedef struct AstComprFor {
  AstNode* target;
  AstNode* iter;
} AstComprFor;

// 函数参数（T007 仅填 name；defaultExpr/rest 形态 T008 复用）
typedef struct AstParam {
  const char* name;
  int32_t nameLen;
  AstNode* defaultExpr;
  bool rest;
} AstParam;

// 推导式子类型（as.compr.compKind）
enum {
  AST_COMP_LIST = 1,
  AST_COMP_MAP = 2,
  AST_COMP_SET = 3,
};
```

In the same file's `union` (after the `memberExpr` member):

```c
    struct {
      AstNode* operand;
      AstUnaryOp op;
    } unary;
    struct {
      AstNode* cond;
      AstNode* thenExpr;
      AstNode* elseExpr;
    } ternary;  // AST_TERNARY（右结合，03 §7）
    struct {
      AstNode* callee;
      AstNode** args;
      bool* spread;  // 与 args 平行：位置 i 为 * 展开实参
      int32_t count;
    } call;
    struct {
      AstNode* obj;
      AstNode* start;  // 三者均可 NULL（端点/步进省略）
      AstNode* end;
      AstNode* step;
    } slice;
    struct {
      AstNode** keys;
      AstNode** values;
      int32_t count;
    } mapLit;  // AST_MAP
    struct {
      ms_u8 compKind;  // AST_COMP_*；AST_GEN_EXPR 恒 0
      AstNode* key;    // map 推导式键；其余 NULL
      AstNode* value;  // list/set 元素或 map 值
      AstComprFor* fors;
      int32_t forCount;
      AstNode** conds;
      int32_t condCount;
    } compr;  // AST_COMPREHENSION / AST_GEN_EXPR 共用
    struct {
      AstParam* params;
      int32_t paramCount;
      AstNode* body;  // AST_BLOCK
    } funcExpr;
```

Also update the comment on `AST_BINARY` line to `// as.binary（逻辑短路 AST_LOGIC 复用 binary 载荷）`, on `AST_LIST`/`AST_SET` to note `// as.list 复用`， and on `AST_RETURN` to note `// 载荷复用 as.exprStmt（value）`.

- [ ] **Step 2: Add error codes in `src/parser/parser.h`**

After `MSE_SYN_STAR_NO_ASSIGN = 1008,` add:

```c
  MSE_SYN_GENEXPR_POS = 1009,     // 生成器表达式仅可作调用唯一实参或赋值右值（03 §1）
  MSE_SYN_SPREAD_NOT_LAST = 1010, // * 展开实参必须是最后一个实参（03 §4）
```

- [ ] **Step 3: Write failing tests (create `tests/unit/test_parser_expr.c`)**

```c
// T007：Parser 表达式单测（docs/tasks/T007 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "parser/parser.h"
#include "test_harness.h"

static AstNode* parseSrc(const char* src, AstArena* arena, MsDiagSink* diags) {
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", arena, diags);
  return parserParseModule(&p);
}

// 首语句（表达式语句）载荷
static AstNode* expr0(AstNode* mod) { return mod->as.list.items[0]->as.exprStmt.expr; }

TEST(precedenceMulOverAdd) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("1 + 2 * 3", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  AstNode* add = expr0(mod);
  EXPECT_EQ(ctx, AST_BINARY, add->kind);
  EXPECT_EQ(ctx, AST_OP_ADD, add->as.binary.op);
  EXPECT_EQ(ctx, AST_INT, add->as.binary.lhs->kind);
  AstNode* mul = add->as.binary.rhs;
  EXPECT_EQ(ctx, AST_BINARY, mul->kind);
  EXPECT_EQ(ctx, AST_OP_MUL, mul->as.binary.op);
  EXPECT_EQ(ctx, 2, (int)mul->as.binary.lhs->as.intLit.v);
  EXPECT_EQ(ctx, 3, (int)mul->as.binary.rhs->as.intLit.v);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(subLeftAssociative) {
  // 10 - 3 - 2 → SUB(SUB(10,3), 2)
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("10 - 3 - 2", &arena, &diags);
  AstNode* sub = expr0(mod);
  EXPECT_EQ(ctx, AST_OP_SUB, sub->as.binary.op);
  EXPECT_EQ(ctx, AST_BINARY, sub->as.binary.lhs->kind);
  EXPECT_EQ(ctx, AST_OP_SUB, sub->as.binary.lhs->as.binary.op);
  EXPECT_EQ(ctx, 10, (int)sub->as.binary.lhs->as.binary.lhs->as.intLit.v);
  EXPECT_EQ(ctx, 3, (int)sub->as.binary.lhs->as.binary.rhs->as.intLit.v);
  EXPECT_EQ(ctx, 2, (int)sub->as.binary.rhs->as.intLit.v);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
TEST(powRightAssociative) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("2 ** 3 ** 2", &arena, &diags);
  AstNode* pow = expr0(mod);
  EXPECT_EQ(ctx, AST_BINARY, pow->kind);
  EXPECT_EQ(ctx, AST_OP_POW, pow->as.binary.op);
  EXPECT_EQ(ctx, 2, (int)pow->as.binary.lhs->as.intLit.v);
  AstNode* rhs = pow->as.binary.rhs;
  EXPECT_EQ(ctx, AST_BINARY, rhs->kind);
  EXPECT_EQ(ctx, AST_OP_POW, rhs->as.binary.op);
  EXPECT_EQ(ctx, 3, (int)rhs->as.binary.lhs->as.intLit.v);
  EXPECT_EQ(ctx, 2, (int)rhs->as.binary.rhs->as.intLit.v);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(unaryBelowPow) {
  // -2 ** 2 → -(2 ** 2)；2 ** -3 右操作数吸收一元（T007 §6 风险条款）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("-2 ** 2", &arena, &diags);
  AstNode* neg = expr0(mod);
  EXPECT_EQ(ctx, AST_UNARY, neg->kind);
  EXPECT_EQ(ctx, AST_UNOP_NEG, neg->as.unary.op);
  AstNode* pow = neg->as.unary.operand;
  EXPECT_EQ(ctx, AST_OP_POW, pow->as.binary.op);
  EXPECT_EQ(ctx, 2, (int)pow->as.binary.lhs->as.intLit.v);
  EXPECT_EQ(ctx, 2, (int)pow->as.binary.rhs->as.intLit.v);
  astArenaDestroy(&arena);
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  mod = parseSrc("2 ** -3", &arena, &diags);
  AstNode* outer = expr0(mod);
  EXPECT_EQ(ctx, AST_OP_POW, outer->as.binary.op);
  EXPECT_EQ(ctx, AST_UNARY, outer->as.binary.rhs->kind);
  EXPECT_EQ(ctx, AST_UNOP_NEG, outer->as.binary.rhs->as.unary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(unaryForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("~x\n!y\nnot z", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, AST_UNOP_INV, expr0(mod)->as.unary.op);
  AstNode* bang = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_UNOP_NOT, bang->as.unary.op);
  AstNode* notkw = mod->as.list.items[2]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_UNOP_NOT, notkw->as.unary.op);
  EXPECT_EQ(ctx, AST_IDENT, notkw->as.unary.operand->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(bitLevels) {
  // a | b ^ c & d << 2 → OR(a, XOR(b, AND(c, SHL(d, 2))))
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a | b ^ c & d << 2", &arena, &diags);
  AstNode* or = expr0(mod);
  EXPECT_EQ(ctx, AST_OP_BIT_OR, or->as.binary.op);
  AstNode* xor = or->as.binary.rhs;
  EXPECT_EQ(ctx, AST_OP_BIT_XOR, xor->as.binary.op);
  AstNode* and = xor->as.binary.rhs;
  EXPECT_EQ(ctx, AST_OP_BIT_AND, and->as.binary.op);
  AstNode* shl = and->as.binary.rhs;
  EXPECT_EQ(ctx, AST_OP_SHL, shl->as.binary.op);
  EXPECT_EQ(ctx, 2, (int)shl->as.binary.rhs->as.intLit.v);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(compareOpsIncludingIsIn) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x == y\na is b\nc in d", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, AST_OP_EQ, expr0(mod)->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_IS, mod->as.list.items[1]->as.exprStmt.expr->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_IN, mod->as.list.items[2]->as.exprStmt.expr->as.binary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
int main(void) {
  TestCtx ctx = {0};
  precedenceMulOverAdd(&ctx);
  subLeftAssociative(&ctx);
  powRightAssociative(&ctx);
  unaryBelowPow(&ctx);
  unaryForms(&ctx);
  bitLevels(&ctx);
  compareOpsIncludingIsIn(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
```

Register in `tests/CMakeLists.txt` (append):

```cmake
ms_add_test(parser_expr ms_parser)
```

- [ ] **Step 4: Run tests to verify they fail**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
```

Expected: FAIL (`AST_UNARY`/`AST_OP_POW` checks fail — current parser has no ladder; `-2 ** 2` etc. produce errors or wrong shapes). Exit code 1.

- [ ] **Step 5: Implement the ladder in `src/parser/parser.c`**

Keep the forward declaration `static AstNode* parseExpr(Parser* p);`, `parseStringLit`, `parsePrimary`, `parsePostfix`, and `parseExprList` in place. Delete the old minimal `parseExpr` function (the one with the `comparisonOpOf` single-shot body) and put all of the following code where it stood (i.e. after `parsePostfix`):

```c
// ---------------- 表达式（完整优先级阶梯，T007 §4.1） ----------------

static AstNode* parseExpr(Parser* p);

// GEN_EXPR 位置限制（03 §1）：仅调用唯一实参或赋值右值（带括号），其余直接操作数位置拒绝
static bool rejectGenExpr(Parser* p, const AstNode* n) {
  if (n == NULL || n->kind != AST_GEN_EXPR) {
    return false;
  }
  errPos(p, n->line, n->col, 1, MSE_SYN_GENEXPR_POS,
         "generator expression is only allowed as the sole call argument or an assignment right-hand side");
  return true;
}

// 二元构造（各级共用）：操作数不得为 GEN_EXPR
static AstNode* mkBinary(Parser* p, AstNode* lhs, AstBinOp op, AstNode* rhs) {
  if (rejectGenExpr(p, lhs) || rejectGenExpr(p, rhs)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_BINARY, lhs->line, lhs->col);
  node->as.binary.lhs = lhs;
  node->as.binary.rhs = rhs;
  node->as.binary.op = op;
  return node;
}

// 逻辑短路构造（AST_LOGIC 复用 binary 载荷，op 取 AND/OR）
static AstNode* mkLogic(Parser* p, AstNode* lhs, AstBinOp op, AstNode* rhs) {
  if (rejectGenExpr(p, lhs) || rejectGenExpr(p, rhs)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_LOGIC, lhs->line, lhs->col);
  node->as.binary.lhs = lhs;
  node->as.binary.rhs = rhs;
  node->as.binary.op = op;
  return node;
}

static AstNode* mkUnary(Parser* p, AstUnaryOp op, AstNode* operand, int32_t line, int32_t col) {
  if (rejectGenExpr(p, operand)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_UNARY, line, col);  // 取运算符位置
  node->as.unary.op = op;
  node->as.unary.operand = operand;
  return node;
}
```

Then (still replacing the old minimal-expression code) add the level functions:

```c
static AstNode* parsePow(Parser* p) {
  AstNode* base = parsePostfix(p);
  if (base == NULL) {
    return NULL;
  }
  if (!match(p, TK_STAR_STAR)) {
    return base;
  }
  AstNode* rhs = parseUnary(p);  // 右结合；rhs 吸收一元（2 ** -3 合法）
  if (rhs == NULL) {
    return NULL;
  }
  return mkBinary(p, base, AST_OP_POW, rhs);
}

static AstNode* parseUnary(Parser* p) {
  AstUnaryOp op;
  switch (p->cur.type) {
    case TK_MINUS:
      op = AST_UNOP_NEG;
      break;
    case TK_BANG:
    case TK_NOT:
      op = AST_UNOP_NOT;
      break;
    case TK_TILDE:
      op = AST_UNOP_INV;
      break;
    default:
      return parsePow(p);
  }
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);
  AstNode* operand = parseUnary(p);  // 一元在 ** 之下：-2 ** 2 → -(2 ** 2)
  if (operand == NULL) {
    return NULL;
  }
  return mkUnary(p, op, operand, line, col);
}
```
```c
// 左结合二元层通用循环：next 为更高层，fromTok 无操作时返回 AST_OP_NONE
static AstNode* parseBinaryLevel(Parser* p, AstNode* (*next)(Parser*), AstBinOp (*fromTok)(MsTokenType)) {
  AstNode* lhs = next(p);
  if (lhs == NULL) {
    return NULL;
  }
  for (;;) {
    AstBinOp op = fromTok(p->cur.type);
    if (op == AST_OP_NONE) {
      return lhs;
    }
    advance(p);
    AstNode* rhs = next(p);
    if (rhs == NULL) {
      return NULL;
    }
    lhs = mkBinary(p, lhs, op, rhs);
    if (lhs == NULL) {
      return NULL;
    }
  }
}

static AstBinOp mulOpOf(MsTokenType type) {
  switch (type) {
    case TK_STAR:
      return AST_OP_MUL;
    case TK_SLASH:
      return AST_OP_DIV;
    case TK_PERCENT:
      return AST_OP_MOD;
    default:
      return AST_OP_NONE;
  }
}

static AstBinOp addOpOf(MsTokenType type) {
  switch (type) {
    case TK_PLUS:
      return AST_OP_ADD;
    case TK_MINUS:
      return AST_OP_SUB;
    default:
      return AST_OP_NONE;
  }
}

static AstBinOp shiftOpOf(MsTokenType type) {
  switch (type) {
    case TK_LT_LT:
      return AST_OP_SHL;
    case TK_GT_GT:
      return AST_OP_SHR;
    default:
      return AST_OP_NONE;
  }
}

static AstBinOp bitAndOpOf(MsTokenType type) {
  return type == TK_AMP ? AST_OP_BIT_AND : AST_OP_NONE;
}

static AstBinOp bitXorOpOf(MsTokenType type) {
  return type == TK_CARET ? AST_OP_BIT_XOR : AST_OP_NONE;
}

static AstBinOp bitOrOpOf(MsTokenType type) {
  return type == TK_PIPE ? AST_OP_BIT_OR : AST_OP_NONE;
}

static AstNode* parseMul(Parser* p) { return parseBinaryLevel(p, parseUnary, mulOpOf); }

static AstNode* parseAdd(Parser* p) { return parseBinaryLevel(p, parseMul, addOpOf); }

static AstNode* parseShift(Parser* p) { return parseBinaryLevel(p, parseAdd, shiftOpOf); }

static AstNode* parseBitAnd(Parser* p) { return parseBinaryLevel(p, parseShift, bitAndOpOf); }

static AstNode* parseBitXor(Parser* p) { return parseBinaryLevel(p, parseBitAnd, bitXorOpOf); }

static AstNode* parseBitOr(Parser* p) { return parseBinaryLevel(p, parseBitXor, bitOrOpOf); }
```

Extend `comparisonOpOf` (existing function) with `is`/`in` before its `default:`:

```c
    case TK_IS:
      return AST_OP_IS;
    case TK_IN:
      return AST_OP_IN;
```

Replace the old `parseExpr` body with the new compare level + entry:

```c
// 比较（03 优先级级 4）：单次、不可链式
static AstNode* parseCompare(Parser* p) {
  AstNode* lhs = parseBitOr(p);
  if (lhs == NULL) {
    return NULL;
  }
  AstBinOp op = comparisonOpOf(p->cur.type);
  if (op == AST_OP_NONE) {
    return lhs;
  }
  advance(p);
  AstNode* rhs = parseBitOr(p);
  if (rhs == NULL) {
    return NULL;
  }
  if (comparisonOpOf(p->cur.type) != AST_OP_NONE) {
    errTok(p, &p->cur, MSE_SYN_CHAINED_CMP, "comparison operators cannot be chained");
    return NULL;
  }
  return mkBinary(p, lhs, op, rhs);
}

static AstNode* parseExpr(Parser* p) { return parseCompare(p); }
```

Note: logic (`&&`/`and` etc.) and ternary come in Task 2 — `parseExpr` temporarily returns compare level. T006's existing tests (`parseComparisonExpr`, `errChainedComparison`, postfix/assignment tests) must stay green.

- [ ] **Step 6: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
.\build\tests\Debug\test_parser_basic.exe
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `all tests passed`, exit 0; full ctest green.

- [ ] **Step 7: Commit**

```powershell
git add src/parser/ast.h src/parser/parser.h src/parser/parser.c tests/unit/test_parser_expr.c tests/CMakeLists.txt
git commit -m "✨ feat(parser): add binary precedence ladder with unary and pow"
```
---

### Task 2: Short-circuit logic and ternary

**Files:**
- Modify: `src/parser/parser.c`, `tests/unit/test_parser_expr.c`

- [ ] **Step 1: Write failing tests (add before `main`, register in `main`)**

```c
TEST(logicSpellings) {
  // a and b or not c → OR(AND(a,b), NOT(c))
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a and b or not c", &arena, &diags);
  AstNode* or = expr0(mod);
  EXPECT_EQ(ctx, AST_LOGIC, or->kind);
  EXPECT_EQ(ctx, AST_OP_OR, or->as.binary.op);
  AstNode* and = or->as.binary.lhs;
  EXPECT_EQ(ctx, AST_LOGIC, and->kind);
  EXPECT_EQ(ctx, AST_OP_AND, and->as.binary.op);
  AstNode* not = or->as.binary.rhs;
  EXPECT_EQ(ctx, AST_UNARY, not->kind);
  EXPECT_EQ(ctx, AST_UNOP_NOT, not->as.unary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(logicSymbolForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a && b || c && d", &arena, &diags);
  AstNode* or = expr0(mod);
  EXPECT_EQ(ctx, AST_OP_OR, or->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_AND, or->as.binary.lhs->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_AND, or->as.binary.rhs->as.binary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(logicBindsLooserThanCompare) {
  // x == 1 && y < 2 → AND(EQ(x,1), LT(y,2))
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x == 1 && y < 2", &arena, &diags);
  AstNode* and = expr0(mod);
  EXPECT_EQ(ctx, AST_LOGIC, and->kind);
  EXPECT_EQ(ctx, AST_OP_EQ, and->as.binary.lhs->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_LT, and->as.binary.rhs->as.binary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(ternaryRightAssociative) {
  // a ? b : c ? d : e → TERNARY(a, b, TERNARY(c, d, e))
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a ? b : c ? d : e", &arena, &diags);
  AstNode* outer = expr0(mod);
  EXPECT_EQ(ctx, AST_TERNARY, outer->kind);
  EXPECT_EQ(ctx, AST_IDENT, outer->as.ternary.cond->kind);
  EXPECT_EQ(ctx, AST_IDENT, outer->as.ternary.thenExpr->kind);
  AstNode* inner = outer->as.ternary.elseExpr;
  EXPECT_EQ(ctx, AST_TERNARY, inner->kind);
  EXPECT_EQ(ctx, AST_IDENT, inner->as.ternary.cond->kind);
  EXPECT_EQ(ctx, 8, inner->as.ternary.cond->col);  // 'c' 位于 1-based 列 8
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(ternaryMissingColon) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a ? b c", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}
```
- [ ] **Step 2: Register tests in `main`**

```c
  logicSpellings(&ctx);
  logicSymbolForms(&ctx);
  logicBindsLooserThanCompare(&ctx);
  ternaryRightAssociative(&ctx);
  ternaryMissingColon(&ctx);
```

- [ ] **Step 3: Run tests to verify they fail**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
```

Expected: FAIL (`AST_LOGIC`/`AST_TERNARY` never produced — `a and b or not c` yields chained-compare/unexpected-token diagnostics). Exit 1.

- [ ] **Step 4: Implement logic + ternary levels**

In `src/parser/parser.c`, after `parseCompare` and before `parseExpr`, add:

```c
// 逻辑与（03 优先级级 3）：&& / and
static AstNode* parseAnd(Parser* p) {
  AstNode* lhs = parseCompare(p);
  if (lhs == NULL) {
    return NULL;
  }
  while (check(p, TK_AMP_AMP) || check(p, TK_AND)) {
    advance(p);
    AstNode* rhs = parseCompare(p);
    if (rhs == NULL) {
      return NULL;
    }
    lhs = mkLogic(p, lhs, AST_OP_AND, rhs);
    if (lhs == NULL) {
      return NULL;
    }
  }
  return lhs;
}

// 逻辑或（03 优先级级 2）：|| / or
static AstNode* parseOr(Parser* p) {
  AstNode* lhs = parseAnd(p);
  if (lhs == NULL) {
    return NULL;
  }
  while (check(p, TK_PIPE_PIPE) || check(p, TK_OR)) {
    advance(p);
    AstNode* rhs = parseAnd(p);
    if (rhs == NULL) {
      return NULL;
    }
    lhs = mkLogic(p, lhs, AST_OP_OR, rhs);
    if (lhs == NULL) {
      return NULL;
    }
  }
  return lhs;
}

// 三元（03 §7）：右结合（分支递归本层），分支惰性求值由编译期保证
static AstNode* parseTernary(Parser* p) {
  AstNode* cond = parseOr(p);
  if (cond == NULL) {
    return NULL;
  }
  if (!match(p, TK_QUESTION)) {
    return cond;
  }
  AstNode* thenExpr = parseTernary(p);
  if (thenExpr == NULL) {
    return NULL;
  }
  if (!match(p, TK_COLON)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ':' in conditional expression");
    return NULL;
  }
  AstNode* elseExpr = parseTernary(p);
  if (elseExpr == NULL) {
    return NULL;
  }
  if (rejectGenExpr(p, cond) || rejectGenExpr(p, thenExpr) || rejectGenExpr(p, elseExpr)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_TERNARY, cond->line, cond->col);
  node->as.ternary.cond = cond;
  node->as.ternary.thenExpr = thenExpr;
  node->as.ternary.elseExpr = elseExpr;
  return node;
}
```

Change the entry point:

```c
static AstNode* parseExpr(Parser* p) { return parseTernary(p); }
```

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `all tests passed`; full ctest green.

- [ ] **Step 6: Commit**

```powershell
git add src/parser/parser.c tests/unit/test_parser_expr.c
git commit -m "✨ feat(parser): add short-circuit logic and ternary expressions"
```
---

### Task 3: Call arguments and slice postfix

**Files:**
- Modify: `src/parser/parser.c`, `tests/unit/test_parser_expr.c`

- [ ] **Step 1: Write failing tests**

```c
TEST(postfixCallIndexChain) {
  // obj.m(x)[i] → INDEX(CALL(MEMBER(obj,m),[x]), i)
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("obj.m(x)[i]", &arena, &diags);
  AstNode* index = expr0(mod);
  EXPECT_EQ(ctx, AST_INDEX, index->kind);
  AstNode* call = index->as.indexExpr.obj;
  EXPECT_EQ(ctx, AST_CALL, call->kind);
  AstNode* member = call->as.call.callee;
  EXPECT_EQ(ctx, AST_MEMBER, member->kind);
  EXPECT_EQ(ctx, 1, call->as.call.count);
  EXPECT_EQ(ctx, AST_IDENT, call->as.call.args[0]->kind);
  EXPECT_EQ(ctx, AST_IDENT, index->as.indexExpr.index->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(callEmptyAndArgs) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f()\ng(1, a)", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  AstNode* empty = expr0(mod);
  EXPECT_EQ(ctx, AST_CALL, empty->kind);
  EXPECT_EQ(ctx, 0, empty->as.call.count);
  AstNode* two = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, 2, two->as.call.count);
  EXPECT_EQ(ctx, 1, (int)two->as.call.args[0]->as.intLit.v);
  EXPECT(ctx, !two->as.call.spread[0]);
  EXPECT(ctx, !two->as.call.spread[1]);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(callSpreadArg) {
  // f(1, *xs) → CALL args=[1, xs]，spread=[false, true]
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(1, *xs)", &arena, &diags);
  AstNode* call = expr0(mod);
  EXPECT_EQ(ctx, AST_CALL, call->kind);
  EXPECT_EQ(ctx, 2, call->as.call.count);
  EXPECT(ctx, !call->as.call.spread[0]);
  EXPECT(ctx, call->as.call.spread[1]);
  EXPECT_EQ(ctx, AST_IDENT, call->as.call.args[1]->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errSpreadNotLast) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(1, *xs, 2)", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1010, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(sliceForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("xs[a:b:c]\nxs[a:]\nxs[:b]\nxs[::c]\nxs[a:b]\nxs[::]", &arena, &diags);
  EXPECT_EQ(ctx, 6, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* s0 = expr0(mod);
  EXPECT_EQ(ctx, AST_SLICE, s0->kind);
  EXPECT(ctx, s0->as.slice.start != NULL && s0->as.slice.end != NULL && s0->as.slice.step != NULL);
  AstNode* s1 = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT(ctx, s1->as.slice.start != NULL && s1->as.slice.end == NULL && s1->as.slice.step == NULL);
  AstNode* s2 = mod->as.list.items[2]->as.exprStmt.expr;
  EXPECT(ctx, s2->as.slice.start == NULL && s2->as.slice.end != NULL && s2->as.slice.step == NULL);
  AstNode* s3 = mod->as.list.items[3]->as.exprStmt.expr;
  EXPECT(ctx, s3->as.slice.start == NULL && s3->as.slice.end == NULL && s3->as.slice.step != NULL);
  AstNode* s4 = mod->as.list.items[4]->as.exprStmt.expr;
  EXPECT(ctx, s4->as.slice.start != NULL && s4->as.slice.end != NULL && s4->as.slice.step == NULL);
  AstNode* s5 = mod->as.list.items[5]->as.exprStmt.expr;
  EXPECT(ctx, s5->as.slice.start == NULL && s5->as.slice.end == NULL && s5->as.slice.step == NULL);
  astArenaDestroy(&arena);
}

TEST(errCallMissingRparen) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(1", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}
```
- [ ] **Step 2: Register tests in `main`**

```c
  postfixCallIndexChain(&ctx);
  callEmptyAndArgs(&ctx);
  callSpreadArg(&ctx);
  errSpreadNotLast(&ctx);
  sliceForms(&ctx);
  errCallMissingRparen(&ctx);
```

- [ ] **Step 3: Run tests to verify they fail**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
```

Expected: FAIL (`f(...)` currently parses as primary `f` then `(` is unexpected → diagnostics). Exit 1.

- [ ] **Step 4: Implement call + slice postfix**

In `src/parser/parser.c`, add before `parsePostfix`:

```c
typedef struct CallArg {
  AstNode* expr;
  bool spread;
} CallArg;

// 调用实参表（03 §4）：位置实参 + 至多一个尾随 * 展开；实参求值顺序=存放顺序
static AstNode* parseCallArgs(Parser* p, AstNode* callee, int32_t line, int32_t col) {
  advance(p);  // (
  CallArg* tmp = NULL;
  int32_t count = 0, cap = 0;
  while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
    bool star = match(p, TK_STAR);
    AstNode* arg = parseExpr(p);
    if (arg == NULL) {
      free(tmp);
      return NULL;
    }
    MS_ARRAY_PUSH(tmp, count, cap, ((CallArg){arg, star}));
    if (star && !check(p, TK_RPAREN)) {
      errTok(p, &p->cur, MSE_SYN_SPREAD_NOT_LAST, "'*' spread argument must be the last argument");
      free(tmp);
      return NULL;
    }
    if (arg->kind == AST_GEN_EXPR && !check(p, TK_RPAREN)) {
      errTok(p, &p->cur, MSE_SYN_GENEXPR_POS, "generator expression must be the sole argument");
      free(tmp);
      return NULL;
    }
    if (!match(p, TK_COMMA)) {
      break;
    }
  }
  if (!match(p, TK_RPAREN)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
    free(tmp);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_CALL, line, col);
  node->as.call.callee = callee;
  node->as.call.count = count;
  node->as.call.args = astNewNodeArray(p->arena, count);
  node->as.call.spread = astArenaAlloc(p->arena, sizeof(bool) * (size_t)count);
  for (int32_t i = 0; i < count; i++) {
    node->as.call.args[i] = tmp[i].expr;
    node->as.call.spread[i] = tmp[i].spread;
  }
  free(tmp);
  return node;
}

// 切片尾部：已消费 '[' 与可选 start，当前 token 为 ':'（03 §3 端点/步进均可省略）
static AstNode* parseSliceRest(Parser* p, AstNode* obj, AstNode* start, int32_t line, int32_t col) {
  advance(p);  // :
  AstNode* end = NULL;
  if (!check(p, TK_COLON) && !check(p, TK_RBRACKET)) {
    end = parseExpr(p);
    if (end == NULL) {
      return NULL;
    }
  }
  AstNode* step = NULL;
  if (match(p, TK_COLON) && !check(p, TK_RBRACKET)) {
    step = parseExpr(p);
    if (step == NULL) {
      return NULL;
    }
  }
  if (!match(p, TK_RBRACKET)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ']'");
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_SLICE, line, col);
  node->as.slice.obj = obj;
  node->as.slice.start = start;
  node->as.slice.end = end;
  node->as.slice.step = step;
  return node;
}
```

Replace the body of the `parsePostfix` loop (keep the `parsePrimary` call prologue) with:

```c
  for (;;) {
    if (rejectGenExpr(p, obj)) {  // GEN_EXPR 不可再套后缀
      return NULL;
    }
    if (check(p, TK_LPAREN)) {
      obj = parseCallArgs(p, obj, obj->line, obj->col);
      if (obj == NULL) {
        return NULL;
      }
    } else if (check(p, TK_LBRACKET)) {
      int32_t line = obj->line, col = obj->col;  // 构造起点取对象起点
      advance(p);
      if (check(p, TK_COLON)) {  // [:…] start 省略
        obj = parseSliceRest(p, obj, NULL, line, col);
        if (obj == NULL) {
          return NULL;
        }
        continue;
      }
      AstNode* first = parseExpr(p);
      if (first == NULL) {
        return NULL;
      }
      if (check(p, TK_COLON)) {
        obj = parseSliceRest(p, obj, first, line, col);
        if (obj == NULL) {
          return NULL;
        }
      } else {
        if (!match(p, TK_RBRACKET)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ']'");
          return NULL;
        }
        AstNode* node = astNewNode(p->arena, AST_INDEX, line, col);
        node->as.indexExpr.obj = obj;
        node->as.indexExpr.index = first;
        obj = node;
      }
    } else if (check(p, TK_DOT)) {
      int32_t line = obj->line, col = obj->col;
      advance(p);
      if (!check(p, TK_ID)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected member name after '.'");
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_MEMBER, line, col);
      node->as.memberExpr.obj = obj;
      node->as.memberExpr.name = p->cur.start;
      node->as.memberExpr.nameLen = p->cur.len;
      advance(p);
      obj = node;
    } else {
      return obj;
    }
  }
```

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `all tests passed`; full ctest green.

- [ ] **Step 6: Commit**

```powershell
git add src/parser/parser.c tests/unit/test_parser_expr.c
git commit -m "✨ feat(parser): add call arguments and slice postfix"
```
---

### Task 4: Grouping and tuple paren forms

**Files:**
- Modify: `src/parser/parser.c`, `tests/unit/test_parser_expr.c`

- [ ] **Step 1: Write failing tests**

```c
TEST(groupingParens) {
  // (1 + 2) * 3 → MUL(ADD(1,2), 3)
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("(1 + 2) * 3", &arena, &diags);
  AstNode* mul = expr0(mod);
  EXPECT_EQ(ctx, AST_OP_MUL, mul->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_ADD, mul->as.binary.lhs->as.binary.op);
  EXPECT_EQ(ctx, 3, (int)mul->as.binary.rhs->as.intLit.v);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(tupleParenForms) {
  // () 空 tuple；(1,) 单元素尾逗号；(1, 2) 元组（02 §5.3）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("()\n(1,)\n(1, 2)", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* empty = expr0(mod);
  EXPECT_EQ(ctx, AST_TUPLE, empty->kind);
  EXPECT_EQ(ctx, 0, empty->as.tuple.count);
  AstNode* single = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_TUPLE, single->kind);
  EXPECT_EQ(ctx, 1, single->as.tuple.count);
  AstNode* pair = mod->as.list.items[2]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_TUPLE, pair->kind);
  EXPECT_EQ(ctx, 2, pair->as.tuple.count);
  astArenaDestroy(&arena);
}

TEST(parenDoubleGroup) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("((1))", &arena, &diags);
  EXPECT_EQ(ctx, AST_INT, expr0(mod)->kind);  // 双层分组还原为表达式本身
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Register tests in `main`**

```c
  groupingParens(&ctx);
  tupleParenForms(&ctx);
  parenDoubleGroup(&ctx);
```

- [ ] **Step 3: Run tests to verify they fail**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
```

Expected: FAIL (`(` is not a primary → "expected expression" diagnostics). Exit 1.

- [ ] **Step 4: Implement TK_LPAREN primary**

In `parsePrimary` in `src/parser/parser.c`, add before the `default:` case:

```c
    case TK_LPAREN: {
      advance(p);  // (
      if (match(p, TK_RPAREN)) {  // () 空 tuple（02 §5.3）
        AstNode* node = astNewNode(p->arena, AST_TUPLE, line, col);
        node->as.tuple.count = 0;
        return node;
      }
      AstNode* first = parseExpr(p);
      if (first == NULL) {
        return NULL;
      }
      if (!check(p, TK_COMMA)) {
        if (!match(p, TK_RPAREN)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
          return NULL;
        }
        return first;  // (expr) 分组（03 §10）
      }
      AstNode** tmp = NULL;
      int32_t count = 0, cap = 0;
      MS_ARRAY_PUSH(tmp, count, cap, first);
      while (match(p, TK_COMMA)) {
        if (check(p, TK_RPAREN)) {
          break;  // 尾逗号 (1,)
        }
        AstNode* e = parseExpr(p);
        if (e == NULL) {
          free(tmp);
          return NULL;
        }
        MS_ARRAY_PUSH(tmp, count, cap, e);
      }
      if (!match(p, TK_RPAREN)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
        free(tmp);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_TUPLE, line, col);
      node->as.tuple.count = count;
      node->as.tuple.items = astNewNodeArray(p->arena, count);
      if (count > 0) {
        memcpy(node->as.tuple.items, tmp, sizeof(AstNode*) * (size_t)count);
      }
      free(tmp);
      return node;
    }
```

Note: the `TK_FOR` genexpr branch is NOT added yet (Task 6); `(x for x in xs)` currently errors with 1001 — acceptable intermediate state.

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `all tests passed`; full ctest green.

- [ ] **Step 6: Commit**

```powershell
git add src/parser/parser.c tests/unit/test_parser_expr.c
git commit -m "✨ feat(parser): add grouping and tuple paren forms"
```
---

### Task 5: Container literals and comprehensions

**Files:**
- Modify: `src/parser/parser.c`, `tests/unit/test_parser_expr.c`

- [ ] **Step 1: Write failing tests**

```c
TEST(listLiterals) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("[]\n[1]\n[1, 2, 3,]", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* empty = expr0(mod);
  EXPECT_EQ(ctx, AST_LIST, empty->kind);
  EXPECT_EQ(ctx, 0, empty->as.list.count);
  EXPECT_EQ(ctx, 1, mod->as.list.items[1]->as.exprStmt.expr->as.list.count);
  AstNode* many = mod->as.list.items[2]->as.exprStmt.expr;
  EXPECT_EQ(ctx, 3, many->as.list.count);  // 尾逗号 tolerated
  astArenaDestroy(&arena);
}

TEST(listComprehension) {
  // [x*x for x in xs if x % 2 == 0] → COMP(LIST, value=MUL, forCount=1, condCount=1)
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("[x*x for x in xs if x % 2 == 0]", &arena, &diags);
  AstNode* comp = expr0(mod);
  EXPECT_EQ(ctx, AST_COMPREHENSION, comp->kind);
  EXPECT_EQ(ctx, AST_COMP_LIST, (int)comp->as.compr.compKind);
  EXPECT_EQ(ctx, AST_OP_MUL, comp->as.compr.value->as.binary.op);
  EXPECT(ctx, comp->as.compr.key == NULL);
  EXPECT_EQ(ctx, 1, comp->as.compr.forCount);
  EXPECT_EQ(ctx, AST_IDENT, comp->as.compr.fors[0].target->kind);
  EXPECT_EQ(ctx, AST_IDENT, comp->as.compr.fors[0].iter->kind);
  EXPECT_EQ(ctx, 1, comp->as.compr.condCount);
  EXPECT_EQ(ctx, AST_OP_EQ, comp->as.compr.conds[0]->as.binary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(comprehensionTwoFors) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("[x+y for x in a for y in b]", &arena, &diags);
  AstNode* comp = expr0(mod);
  EXPECT_EQ(ctx, AST_COMPREHENSION, comp->kind);
  EXPECT_EQ(ctx, 2, comp->as.compr.forCount);
  EXPECT_EQ(ctx, AST_IDENT, comp->as.compr.fors[0].target->kind);
  EXPECT_EQ(ctx, AST_IDENT, comp->as.compr.fors[1].target->kind);
  EXPECT_EQ(ctx, 0, comp->as.compr.condCount);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(mapSetFiveForms) {
  // {} → 空 map；{1:2} → map；{1,2} → set；{k:v for…} → map 推导；{x for…} → set 推导
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{}\n{1: 2}\n{1, 2}\n{k: v for k, v in m}\n{x % 3 for x in xs}", &arena, &diags);
  EXPECT_EQ(ctx, 5, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* emptyMap = expr0(mod);
  EXPECT_EQ(ctx, AST_MAP, emptyMap->kind);
  EXPECT_EQ(ctx, 0, emptyMap->as.mapLit.count);
  AstNode* mapLit = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_MAP, mapLit->kind);
  EXPECT_EQ(ctx, 1, mapLit->as.mapLit.count);
  EXPECT_EQ(ctx, 1, (int)mapLit->as.mapLit.keys[0]->as.intLit.v);
  EXPECT_EQ(ctx, 2, (int)mapLit->as.mapLit.values[0]->as.intLit.v);
  AstNode* setLit = mod->as.list.items[2]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_SET, setLit->kind);
  EXPECT_EQ(ctx, 2, setLit->as.list.count);
  AstNode* mapComp = mod->as.list.items[3]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_COMPREHENSION, mapComp->kind);
  EXPECT_EQ(ctx, AST_COMP_MAP, (int)mapComp->as.compr.compKind);
  EXPECT_EQ(ctx, AST_IDENT, mapComp->as.compr.key->kind);
  EXPECT_EQ(ctx, AST_IDENT, mapComp->as.compr.value->kind);
  EXPECT_EQ(ctx, 1, mapComp->as.compr.forCount);
  EXPECT_EQ(ctx, AST_TUPLE, mapComp->as.compr.fors[0].target->kind);  // for k, v 多变量目标
  EXPECT_EQ(ctx, 2, mapComp->as.compr.fors[0].target->as.tuple.count);
  AstNode* setComp = mod->as.list.items[4]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_COMPREHENSION, setComp->kind);
  EXPECT_EQ(ctx, AST_COMP_SET, (int)setComp->as.compr.compKind);
  EXPECT(ctx, setComp->as.compr.key == NULL);
  EXPECT_EQ(ctx, AST_OP_MOD, setComp->as.compr.value->as.binary.op);
  astArenaDestroy(&arena);
}

TEST(singleElemSetLiteral) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{x}", &arena, &diags);
  AstNode* set = expr0(mod);
  EXPECT_EQ(ctx, AST_SET, set->kind);
  EXPECT_EQ(ctx, 1, set->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errComprBadTarget) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("[x for 1 in xs]", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}
```
- [ ] **Step 2: Register tests in `main`**

```c
  listLiterals(&ctx);
  listComprehension(&ctx);
  comprehensionTwoFors(&ctx);
  mapSetFiveForms(&ctx);
  singleElemSetLiteral(&ctx);
  errComprBadTarget(&ctx);
```

- [ ] **Step 3: Run tests to verify they fail**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
```

Expected: FAIL (`[`/`{` are not primaries → "expected expression" diagnostics). Exit 1.

- [ ] **Step 4: Implement parseComprTail + bracket/brace primaries**

Add before `parsePrimary` in `src/parser/parser.c` (after `parseSliceRest`/call code):

```c
// 推导式/生成器公共尾部（03 §2）：(for 目标 in 迭代对象 | if 条件)*，末尾闭括号 closer
// compKind 取 AST_COMP_LIST/MAP/SET；0 为生成器表达式（AST_GEN_EXPR）
static AstNode* parseComprTail(Parser* p, AstNode* key, AstNode* value, int32_t compKind, MsTokenType closer,
                               int32_t line, int32_t col) {
  AstComprFor* fors = NULL;
  int32_t forCount = 0, forCap = 0;
  AstNode** conds = NULL;
  int32_t condCount = 0, condCap = 0;
  bool bad = false;
  while (!bad) {
    if (match(p, TK_FOR)) {
      AstNode** names = NULL;
      int32_t nameCount = 0, nameCap = 0;
      while (!bad) {
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected identifier in 'for' target");
          bad = true;
          break;
        }
        AstNode* id = astNewNode(p->arena, AST_IDENT, p->cur.line, p->cur.col);
        id->as.ident.name = p->cur.start;
        id->as.ident.nameLen = p->cur.len;
        advance(p);
        MS_ARRAY_PUSH(names, nameCount, nameCap, id);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
      if (bad) {
        free(names);
        break;
      }
      if (!match(p, TK_IN)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'in' after 'for' target");
        free(names);
        bad = true;
        break;
      }
      AstNode* iter = parseExpr(p);
      if (iter == NULL || rejectGenExpr(p, iter)) {
        free(names);
        bad = true;
        break;
      }
      AstNode* target;
      if (nameCount == 1) {
        target = names[0];
      } else {  // for k, v in … → 元组目标
        target = astNewNode(p->arena, AST_TUPLE, names[0]->line, names[0]->col);
        target->as.tuple.count = nameCount;
        target->as.tuple.items = astNewNodeArray(p->arena, nameCount);
        for (int32_t i = 0; i < nameCount; i++) {
          target->as.tuple.items[i] = names[i];
        }
      }
      free(names);
      MS_ARRAY_PUSH(fors, forCount, forCap, ((AstComprFor){target, iter}));
    } else if (match(p, TK_IF)) {
      AstNode* cond = parseExpr(p);
      if (cond == NULL) {
        bad = true;
        break;
      }
      MS_ARRAY_PUSH(conds, condCount, condCap, cond);
    } else {
      break;
    }
  }
  if (!bad && !match(p, closer)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected closing bracket of comprehension");
    bad = true;
  }
  if (bad) {
    free(fors);
    free(conds);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, compKind == 0 ? AST_GEN_EXPR : AST_COMPREHENSION, line, col);
  node->as.compr.compKind = (ms_u8)compKind;
  node->as.compr.key = key;
  node->as.compr.value = value;
  node->as.compr.forCount = forCount;
  node->as.compr.fors = astArenaAlloc(p->arena, sizeof(AstComprFor) * (size_t)forCount);
  if (forCount > 0) {
    memcpy(node->as.compr.fors, fors, sizeof(AstComprFor) * (size_t)forCount);
  }
  node->as.compr.condCount = condCount;
  node->as.compr.conds = astNewNodeArray(p->arena, condCount);
  if (condCount > 0) {
    memcpy(node->as.compr.conds, conds, sizeof(AstNode*) * (size_t)condCount);
  }
  free(fors);
  free(conds);
  return node;
}
```
Add a `MapPair` typedef next to `CallArg` (Task 3 location, before `parseCallArgs`):

```c
typedef struct MapPair {
  AstNode* key;
  AstNode* value;
} MapPair;
```

In `parsePrimary`, add before the `default:` case (after the `TK_LPAREN` case from Task 4):

```c
    case TK_LBRACKET: {  // list 字面量 / list 推导式（03 §2）
      advance(p);
      if (match(p, TK_RBRACKET)) {
        AstNode* node = astNewNode(p->arena, AST_LIST, line, col);
        node->as.list.count = 0;
        return node;
      }
      AstNode* first = parseExpr(p);
      if (first == NULL) {
        return NULL;
      }
      if (check(p, TK_FOR)) {
        return parseComprTail(p, NULL, first, AST_COMP_LIST, TK_RBRACKET, line, col);
      }
      AstNode** tmp = NULL;
      int32_t count = 0, cap = 0;
      MS_ARRAY_PUSH(tmp, count, cap, first);
      while (match(p, TK_COMMA)) {
        if (check(p, TK_RBRACKET)) {
          break;  // 尾逗号
        }
        AstNode* e = parseExpr(p);
        if (e == NULL) {
          free(tmp);
          return NULL;
        }
        MS_ARRAY_PUSH(tmp, count, cap, e);
      }
      if (!match(p, TK_RBRACKET)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ']'");
        free(tmp);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_LIST, line, col);
      node->as.list.count = count;
      node->as.list.items = astNewNodeArray(p->arena, count);
      if (count > 0) {
        memcpy(node->as.list.items, tmp, sizeof(AstNode*) * (size_t)count);
      }
      free(tmp);
      return node;
    }
    case TK_LBRACE: {  // map/set 字面量与推导式（03 §2；歧义按首个子表达式后 token 定形）
      advance(p);
      if (match(p, TK_RBRACE)) {  // {} 空 map（02 §5）
        AstNode* node = astNewNode(p->arena, AST_MAP, line, col);
        node->as.mapLit.count = 0;
        return node;
      }
      AstNode* key = parseExpr(p);
      if (key == NULL) {
        return NULL;
      }
      if (check(p, TK_COLON)) {  // map 族
        advance(p);
        AstNode* value = parseExpr(p);
        if (value == NULL) {
          return NULL;
        }
        if (check(p, TK_FOR)) {
          return parseComprTail(p, key, value, AST_COMP_MAP, TK_RBRACE, line, col);
        }
        MapPair* tmp = NULL;
        int32_t count = 0, cap = 0;
        MS_ARRAY_PUSH(tmp, count, cap, ((MapPair){key, value}));
        while (match(p, TK_COMMA)) {
          if (check(p, TK_RBRACE)) {
            break;  // 尾逗号
          }
          AstNode* k = parseExpr(p);
          if (k == NULL) {
            free(tmp);
            return NULL;
          }
          if (!match(p, TK_COLON)) {
            errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ':' in map literal");
            free(tmp);
            return NULL;
          }
          AstNode* v = parseExpr(p);
          if (v == NULL) {
            free(tmp);
            return NULL;
          }
          MS_ARRAY_PUSH(tmp, count, cap, ((MapPair){k, v}));
        }
        if (!match(p, TK_RBRACE)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}'");
          free(tmp);
          return NULL;
        }
        AstNode* node = astNewNode(p->arena, AST_MAP, line, col);
        node->as.mapLit.count = count;
        node->as.mapLit.keys = astNewNodeArray(p->arena, count);
        node->as.mapLit.values = astNewNodeArray(p->arena, count);
        for (int32_t i = 0; i < count; i++) {
          node->as.mapLit.keys[i] = tmp[i].key;
          node->as.mapLit.values[i] = tmp[i].value;
        }
        free(tmp);
        return node;
      }
      if (check(p, TK_FOR)) {  // set 推导式
        return parseComprTail(p, NULL, key, AST_COMP_SET, TK_RBRACE, line, col);
      }
      AstNode** tmp = NULL;  // set 字面量（{x} 单元素亦然）
      int32_t count = 0, cap = 0;
      MS_ARRAY_PUSH(tmp, count, cap, key);
      while (match(p, TK_COMMA)) {
        if (check(p, TK_RBRACE)) {
          break;
        }
        AstNode* e = parseExpr(p);
        if (e == NULL) {
          free(tmp);
          return NULL;
        }
        MS_ARRAY_PUSH(tmp, count, cap, e);
      }
      if (!match(p, TK_RBRACE)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}'");
        free(tmp);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_SET, line, col);
      node->as.list.count = count;
      node->as.list.items = astNewNodeArray(p->arena, count);
      if (count > 0) {
        memcpy(node->as.list.items, tmp, sizeof(AstNode*) * (size_t)count);
      }
      free(tmp);
      return node;
    }
```

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `all tests passed`; full ctest green.

- [ ] **Step 6: Commit**

```powershell
git add src/parser/parser.c tests/unit/test_parser_expr.c
git commit -m "✨ feat(parser): add container literals and comprehensions"
```
---

### Task 6: Generator expressions and placement checks

**Files:**
- Modify: `src/parser/parser.c`, `tests/unit/test_parser_expr.c`

- [ ] **Step 1: Write failing tests**

```c
TEST(genexprSoleCallArg) {
  // sum(x*x for x in xs) → CALL 单实参 GEN_EXPR（无需再括号）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("sum(x*x for x in xs)", &arena, &diags);
  AstNode* call = expr0(mod);
  EXPECT_EQ(ctx, AST_CALL, call->kind);
  EXPECT_EQ(ctx, 1, call->as.call.count);
  AstNode* gen = call->as.call.args[0];
  EXPECT_EQ(ctx, AST_GEN_EXPR, gen->kind);
  EXPECT_EQ(ctx, 1, gen->as.compr.forCount);
  EXPECT_EQ(ctx, 0, gen->as.compr.condCount);
  EXPECT(ctx, gen->as.compr.key == NULL);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(genexprAssignRhs) {
  // var g = (x for x in xs if x > 0) → 赋值右值（带括号）+ if 条件
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var g = (x for x in xs if x > 0)", &arena, &diags);
  AstNode* decl = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_VAR_DECL, decl->kind);
  EXPECT_EQ(ctx, AST_GEN_EXPR, decl->as.varDecl.init->kind);
  EXPECT_EQ(ctx, 1, decl->as.varDecl.init->as.compr.condCount);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errGenexprAsBinaryOperand) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("1 + (x for x in xs)", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprAsUnaryOperand) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("-(x for x in xs)", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprWithPostfix) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("(x for x in xs)[0]", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprNonSoleArg) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(x for x in xs, 1)", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprInInterp) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("\"a${x for x in xs}\"", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Register tests in `main`**

```c
  genexprSoleCallArg(&ctx);
  genexprAssignRhs(&ctx);
  errGenexprAsBinaryOperand(&ctx);
  errGenexprAsUnaryOperand(&ctx);
  errGenexprWithPostfix(&ctx);
  errGenexprNonSoleArg(&ctx);
  errGenexprInInterp(&ctx);
```

- [ ] **Step 3: Run tests to verify they fail**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
```

Expected: FAIL (no `TK_FOR` branch in the `TK_LPAREN` case yet — `(x for …)` errors 1001, not producing GEN_EXPR; interp check missing). Exit 1.

- [ ] **Step 4: Implement genexpr branch + interp check**

In `parsePrimary`'s `TK_LPAREN` case, after `first` is parsed and before the `!check(p, TK_COMMA)` grouping check, insert:

```c
      if (check(p, TK_FOR)) {  // 生成器表达式（03 §1：惰性，位置限制见 rejectGenExpr）
        return parseComprTail(p, NULL, first, 0, TK_RPAREN, line, col);
      }
```

In `parseStringLit`, after `AstNode* expr = parseExpr(p); if (expr == NULL) {...}`, add:

```c
      if (rejectGenExpr(p, expr)) {
        free(tmp);
        return NULL;
      }
```

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `all tests passed`; full ctest green.

- [ ] **Step 6: Commit**

```powershell
git add src/parser/parser.c tests/unit/test_parser_expr.c
git commit -m "✨ feat(parser): add generator expressions with placement checks"
```
---

### Task 7: Function expressions and return statement

**Files:**
- Modify: `src/parser/parser.c`, `tests/unit/test_parser_expr.c`

- [ ] **Step 1: Write failing tests**

```c
TEST(funcExpression) {
  // var f = func(x) { return x * 2 } → FUNC_EXPR(param x, body=BLOCK[RETURN])
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var f = func(x) { return x * 2 }", &arena, &diags);
  AstNode* decl = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_VAR_DECL, decl->kind);
  AstNode* fn = decl->as.varDecl.init;
  EXPECT_EQ(ctx, AST_FUNC_EXPR, fn->kind);
  EXPECT_EQ(ctx, 1, fn->as.funcExpr.paramCount);
  EXPECT(ctx, fn->as.funcExpr.params[0].nameLen == 1 && fn->as.funcExpr.params[0].name[0] == 'x');
  EXPECT_EQ(ctx, AST_BLOCK, fn->as.funcExpr.body->kind);
  EXPECT_EQ(ctx, 1, fn->as.funcExpr.body->as.list.count);
  AstNode* ret = fn->as.funcExpr.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_RETURN, ret->kind);
  EXPECT_EQ(ctx, AST_OP_MUL, ret->as.exprStmt.expr->as.binary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(funcExprMultiParamsAndBareReturn) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func(a, b) { return }", &arena, &diags);
  AstNode* fn = expr0(mod);
  EXPECT_EQ(ctx, AST_FUNC_EXPR, fn->kind);
  EXPECT_EQ(ctx, 2, fn->as.funcExpr.paramCount);
  AstNode* ret = fn->as.funcExpr.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_RETURN, ret->kind);
  EXPECT(ctx, ret->as.exprStmt.expr == NULL);  // 裸 return
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(funcExprNoParams) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func() { }", &arena, &diags);
  AstNode* fn = expr0(mod);
  EXPECT_EQ(ctx, AST_FUNC_EXPR, fn->kind);
  EXPECT_EQ(ctx, 0, fn->as.funcExpr.paramCount);
  EXPECT_EQ(ctx, AST_BLOCK, fn->as.funcExpr.body->kind);
  EXPECT_EQ(ctx, 0, fn->as.funcExpr.body->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(funcExprBadParam) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func(1) { }", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(interpSegmentOrder) {
  // "a${b}c${d(e)}" → 段序 [str, ident, str, call]
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("\"a${b}c${d(e)}\"", &arena, &diags);
  AstNode* s = expr0(mod);
  EXPECT_EQ(ctx, AST_STRING, s->kind);
  EXPECT_EQ(ctx, 4, s->as.strLit.count);
  EXPECT(ctx, s->as.strLit.segs[0].expr == NULL);
  EXPECT_EQ(ctx, 1, s->as.strLit.segs[0].len);
  EXPECT_EQ(ctx, AST_IDENT, s->as.strLit.segs[1].expr->kind);
  EXPECT(ctx, s->as.strLit.segs[2].expr == NULL);
  EXPECT_EQ(ctx, 1, s->as.strLit.segs[2].len);
  EXPECT_EQ(ctx, AST_CALL, s->as.strLit.segs[3].expr->kind);
  EXPECT_EQ(ctx, 1, s->as.strLit.segs[3].expr->as.call.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errMemberBraceCall) {
  // obj.m(x)[i].f{…} 不合法：后缀链无 { 形态（T007 §5.1 后缀链行）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("obj.m(x)[i].f { }", &arena, &diags);
  EXPECT(ctx, diags.count >= 1);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Register tests in `main`**

```c
  funcExpression(&ctx);
  funcExprMultiParamsAndBareReturn(&ctx);
  funcExprNoParams(&ctx);
  funcExprBadParam(&ctx);
  interpSegmentOrder(&ctx);
  errMemberBraceCall(&ctx);
```

- [ ] **Step 3: Run tests to verify they fail**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
```

Expected: FAIL (`func` is not a primary; `return` is not a statement → diagnostics). Exit 1.

- [ ] **Step 4: Implement func expression + return statement**

In `parsePrimary`, add before the `default:` case:

```c
    case TK_FUNC: {  // 函数表达式（05 §2，一等值；默认参数/*rest/表达式体 T008）
      advance(p);
      if (!match(p, TK_LPAREN)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '(' after 'func'");
        return NULL;
      }
      AstParam* params = NULL;
      int32_t paramCount = 0, cap = 0;
      while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected parameter name");
          free(params);
          return NULL;
        }
        AstParam param = {p->cur.start, p->cur.len, NULL, false};
        advance(p);
        MS_ARRAY_PUSH(params, paramCount, cap, param);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
      if (!match(p, TK_RPAREN)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
        free(params);
        return NULL;
      }
      AstNode* body = parseBlock(p);
      if (body == NULL) {
        free(params);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_FUNC_EXPR, line, col);
      node->as.funcExpr.paramCount = paramCount;
      node->as.funcExpr.params = astArenaAlloc(p->arena, sizeof(AstParam) * (size_t)paramCount);
      if (paramCount > 0) {
        memcpy(node->as.funcExpr.params, params, sizeof(AstParam) * (size_t)paramCount);
      }
      node->as.funcExpr.body = body;
      free(params);
      return node;
    }
```

Note: `parsePrimary` must call `parseBlock`, which is defined later in the file — add a forward declaration near the top of the statement section: `static AstNode* parseBlock(Parser* p);` (place it right after the expression section's forward decl of `parseExpr`).

Add before `parseStatement`:

```c
// return 语句（05 §5）：裸 return 返回 nil；多值 return a, b 即 tuple（T008 补完整函数族）
static AstNode* parseReturnStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // return
  AstNode* value = NULL;
  if (!check(p, TK_RBRACE) && !check(p, TK_EOF) && p->cur.line == p->prevLine) {
    value = parseExprList(p);
    if (value == NULL) {
      return NULL;
    }
  }
  AstNode* node = astNewNode(p->arena, AST_RETURN, line, col);
  node->as.exprStmt.expr = value;  // 载荷复用 exprStmt
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}
```

In `parseStatement`, add after the `TK_IF` check:

```c
  if (check(p, TK_RETURN)) {
    return parseReturnStmt(p);
  }
```

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
.\build\tests\Debug\test_parser_expr.exe
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `all tests passed`; full ctest green (40+ tests total across suites).

- [ ] **Step 6: Commit**

```powershell
git add src/parser/parser.c tests/unit/test_parser_expr.c
git commit -m "✨ feat(parser): add function expressions and return statement"
```
---

### Task 8: Formatting, full verification, task docs

**Files:**
- Modify: `docs/tasks/T007-expression-parsing.md`, `docs/tasks/README.md`
- Possibly reformat: `src/parser/ast.h`, `src/parser/parser.h`, `src/parser/parser.c`, `tests/unit/test_parser_expr.c`

- [ ] **Step 1: clang-format all touched C files**

```powershell
clang-format -i src/parser/ast.h src/parser/parser.h src/parser/parser.c tests/unit/test_parser_expr.c
```

Expected: exit 0. If clang-format is unavailable, skip and match `28-style-c.md` manually (verify 2-space indent, no trailing whitespace).

- [ ] **Step 2: Rebuild + full suite**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Expected: 100% pass, zero failures. This is the DoD gate: T007 §4 all five items hold (each maps to tests delivered in Tasks 1–7).

- [ ] **Step 3: Commit formatting (if diff non-empty)**

```powershell
git add src/parser tests/unit/test_parser_expr.c
git commit -m "🎨 style(parser): clang-format T007 sources"
```

(If `git diff --stat` after formatting is empty, skip this step.)

- [ ] **Step 4: Update task docs**

`docs/tasks/T007-expression-parsing.md` header: `- 状态: 未开始` → `- 状态: ✅`. Tick all DoD checkboxes `- [ ]` → `- [x]`.

`docs/tasks/README.md` row: `| T007 | 表达式解析 | [T007](T007-expression-parsing.md) | P2 | T006 | L | ⬜ |` → status `✅`.

- [ ] **Step 5: Commit docs**

```powershell
git add docs/tasks/T007-expression-parsing.md docs/tasks/README.md
git commit -m "✅ docs(tasks): mark T007 done"
```

- [ ] **Step 6: Final verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure
git log --oneline -10
```

Expected: ctest green; commit trail shows design spec, 6 feature/style commits, docs commit, all on `feat/t007-expression-parsing`.

---

## Self-Review (done at plan time)

- Spec coverage: ladder (T1/T2), postfix call/slice/member (T3), group/tuple parens (T4), containers + comprehensions + disambiguation (T5), genexpr placement + interp check (T6), func expr + return (T7), DoD items each covered by named tests; docs/status (T8). `obj.m(x)[i].f{…}` rejection (T7). `-2**2` (T1). No placeholders.
- Known intentional gaps (documented in spec §5): tuple RHS containing parenthesized genexpr unchecked; `for`-statement itself is T008 (statement-level `for` after an expression errors as "expected newline" — MSE1xxx, acceptable).
- Type consistency: `as.unary.op` type `AstUnaryOp`; `as.call.spread` is `bool*`; `as.compr.compKind` is `ms_u8` cast from `AST_COMP_*`; `AstParam` field order `(name, nameLen, defaultExpr, rest)` used identically in T7 code.
