# T008 复合语句与函数/类声明解析 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete all remaining mslang statement parsing: for (5 forms)/while, value/type switch, break/continue/throw/yield, try/catch/finally, defer, go/channel (send/recv/select), import (3 forms), function declarations (defaults/rest/expression-body/`func*`/`async`), class/trait declarations — syntax only, no semantic checks (scope analysis is T009).

**Architecture:** Recursive-descent additions to `src/parser/parser.c` single file. AST payloads added to `src/parser/ast.h` union. Single-token lookahead extended with one snapshot/restore `peekType` helper (scanner state is plain data) used only for `switch type(`, class `static` prefix, and `for`-post `++`/`--` sugar. A `noIn` parser flag makes `in` a non-operator while parsing a `for` header's first clause (JS-style), giving unambiguous 5-form discrimination without backtracking.

**Tech Stack:** C11, CMake + MSVC (VS 2022 multi-config), custom test harness (`tests/unit/test_harness.h`), ctest.

**Spec:** `docs/tasks/T008-complex-statements.md` + `docs/language/04-statements.md` §4-§10, `05-functions.md` §1 §4 §8, `06-classes.md` §1-§4, `07-errors.md` §1, `08-concurrency.md` §2-§4, `09-modules.md` §2, `03-expressions.md` §8.

---

## Context (read this first)

- Work dir: `C:\Workspace\Repositories\study\aicode\mslang-cc`. Branch: `feat/t008-complex-statements` (already created from main).
- Git repo root is the PARENT directory — run git from the mslang-cc workdir and stage only mslang-cc paths.
- Build: `cmake --build build --config Debug` (VS 2022 multi-config; output in `build/` only).
- Run one unit test: `.\build\tests\Debug\test_parser_complex.exe` (exit 0, prints `all tests passed`).
- Full suite: `ctest --test-dir build -C Debug --output-on-failure`.
- Test style: `TEST(name)` taking `TestCtx* ctx`; `EXPECT/EXPECT_EQ` from `test_harness.h`; helper `parseSrc` parses module; walk `mod->as.list.items[i]`.
- Style: `docs/language/28-style-c.md` (lowerCamelCase, 2-space indent, `//`-only Chinese comments). `goto` allowed only for cleanup jumps.
- Commit format: `<gitmoji> <type>(<scope>): <message>`.

### Key language facts driving the design

- Scanner ASI only fires at parenDepth==0 (module level); inside any `{}` statements end by parser line-number checks (`endSimpleStmt`), so multi-line switch/select/class/try bodies need no new terminator logic.
- `type`, `super`, `static` are NOT keywords (01 §2, 37 reserved words only) — special-cased by token text + peek.
- There is no `++` token and no general increment operator; `i++`/`i--` appear only in 04 §4 for-post examples → supported as for-post-only sugar desugared to `AST_ASSIGN` compound.
- `catch e` (bare catch-all binding) and `catch ValueError` (typed, binding omitted) are syntactically identical (single ID). Resolution: single-ID clause always parses as bare binding (`bindName`, empty type list); multi-ID (`catch A, B`) parses as typed. This satisfies the T008 ordering test (`try{}catch e{}catch V{}` → MSE1xxx). T023/Binder may later reclassify a bare binding whose name resolves to a class. Recorded in T008 task doc 备注.
- Function redefinition is legal (05 §1 "后定义覆盖先定义") → func names are NOT scope-declared nor dup-checked; class names ARE declared (dup with var/class errors).
- `break`/`continue`/`return`-at-top/`await`-position legality are T009 concerns — parse only.
- Payload reuse (documented): `AST_THROW`/`AST_DEFER`/`AST_YIELD` reuse `as.exprStmt.expr`; `AST_RECV`/`AST_AWAIT`/`AST_GO` reuse `as.unary` (op==0); `AST_BREAK`/`AST_CONTINUE`/`AST_THIS` no payload.

## File Structure

- Modify: `src/parser/ast.h` — new kinds `AST_TRY`/`AST_YIELD`/`AST_THIS`; flags `AST_VAR_STATIC`, `AST_FUNC_GENERATOR/ASYNC/STATIC`, `AST_FOR_*`, `AST_SEL_*`; structs `AstCatchClause`, `AstImportName`; union members `forStmt/whileStmt/switchStmt/switchCase/sendStmt/selectStmt/selectCase/importStmt/tryStmt/funcDecl/classDecl`; `funcExpr` gains `flags`/`exprBody`.
- Modify: `src/parser/parser.h` — `MSE_SYN_CATCH_ORDER=1011`, `MSE_SYN_PARAM_ORDER=1012`, `MSE_SYN_DEFER_TOPLEVEL=1013`, `MSE_SYN_DEFER_CALL=1014`, `MSE_SYN_IMPORT_PATH=1015`, `MSE_SYN_CLASS_THIS=1016`; `Parser` gains `noIn`, `funcDepth`.
- Modify: `src/parser/parser.c` — all new parse functions (statement section grows ~700 lines; single file kept per T006/T007 precedent).
- Create: `tests/unit/test_parser_complex.c` — all T008 tests (~35).
- Modify: `tests/CMakeLists.txt` — register `ms_add_test(parser_complex ms_parser)`.
- Modify (final task): `docs/tasks/T008-complex-statements.md` (DoD checkboxes, status ✅, 备注) + `docs/tasks/README.md` (status ✅).

---

### Task 1: Prefix expressions (recv/await/go) + send statement

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.h`, `src/parser/parser.c`
- Create: `tests/unit/test_parser_complex.c`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Register the test target**

In `tests/CMakeLists.txt`, append:

```cmake
ms_add_test(parser_complex ms_parser)
```

- [ ] **Step 2: Write the failing tests (file skeleton + Task 1 tests)**

Create `tests/unit/test_parser_complex.c`:

```c
// T008：Parser 复合语句单测（docs/tasks/T008 §5.1）
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

// 首语句载荷（表达式语句）
static AstNode* expr0(AstNode* mod) { return mod->as.list.items[0]->as.exprStmt.expr; }

TEST(recvExprOnAssignRhs) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("v = <-ch", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  AstNode* assign = expr0(mod);
  EXPECT_EQ(ctx, AST_ASSIGN, assign->kind);
  EXPECT_EQ(ctx, AST_RECV, assign->as.assign.value->kind);
  EXPECT_EQ(ctx, AST_IDENT, assign->as.assign.value->as.unary.operand->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(recvDiscardStmt) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("<-ch", &arena, &diags);
  EXPECT_EQ(ctx, AST_RECV, expr0(mod)->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(awaitAndGoExprs) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x = await f()\ngo worker(1, 2)", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, AST_AWAIT, mod->as.list.items[0]->as.assign.value->kind);
  AstNode* goStmt = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_GO, goStmt->kind);
  EXPECT_EQ(ctx, AST_CALL, goStmt->as.unary.operand->kind);
  EXPECT_EQ(ctx, 2, goStmt->as.unary.operand->as.call.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(sendStmt) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("ch <- v\nxs[0] <- 5", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  AstNode* send = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_SEND, send->kind);
  EXPECT_EQ(ctx, AST_IDENT, send->as.sendStmt.chan->kind);
  EXPECT_EQ(ctx, AST_IDENT, send->as.sendStmt.value->kind);
  AstNode* sendIdx = mod->as.list.items[1];
  EXPECT_EQ(ctx, AST_SEND, sendIdx->kind);
  EXPECT_EQ(ctx, AST_INDEX, sendIdx->as.sendStmt.chan->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(unaryBindsLooserThanRecv) {
  // 03 优先级 12 < 13：-<-ch → NEG(RECV(ch))
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("y = -<-ch", &arena, &diags);
  AstNode* neg = mod->as.list.items[0]->as.assign.value;
  EXPECT_EQ(ctx, AST_UNARY, neg->kind);
  EXPECT_EQ(ctx, AST_RECV, neg->as.unary.operand->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 3: Run to verify failure**

Run: `cmake --build build --config Debug --target test_parser_complex 2>&1 | Select-Object -Last 5` then `.\build\tests\Debug\test_parser_complex.exe`
Expected: compile errors (AST_RECV exists but `as.unary` reuse compiles; send/recv parsing missing → test binary fails or compile fails on AST_SEND payload) — red.

- [ ] **Step 4: Implement**

In `src/parser/parser.c`, insert after `parsePow` (before `parseUnary`):

```c
// 优先级 13 前缀（03 §8）：<-
// 接收 / await / go（复用 unary 载荷，op 恒 0）
static AstNode* parsePrefix13(Parser* p) {
  AstKind kind;
  switch (p->cur.type) {
    case TK_LARROW:
      kind = AST_RECV;
      break;
    case TK_AWAIT:
      kind = AST_AWAIT;
      break;
    case TK_GO:
      kind = AST_GO;
      break;
    default:
      return parsePow(p);
  }
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);
  AstNode* operand = parsePrefix13(p);
  if (operand == NULL || rejectGenExpr(p, operand)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, kind, line, col);
  node->as.unary.operand = operand;
  return node;
}
```

Change `parseUnary`'s `default:` branch from `return parsePow(p);` to `return parsePrefix13(p);`.

Refactor `parseSimpleStmt`: keep its first 9 lines, move everything after `first == NULL` check into a new `finishSimpleStmt` (placed after `finishAssignTargets`, replacing the old body). The `terminate` flag controls whether the terminating `;`/newline is consumed — for-init must leave the header `;` unconsumed (Task 3):

```c
// 首表达式后定形：短声明 / send / 元组目标 / 复合赋值 / 普通赋值 / 表达式语句
// terminate=false 供 for 头部首段使用：不消费结尾 ;/换行（04 §4 判别需要）
static AstNode* finishSimpleStmt(Parser* p, AstNode* first, bool firstStar, int32_t line, int32_t col,
                                 bool terminate) {
  // x := e 短声明（单 IDENT 目标）
  if (!firstStar && check(p, TK_COLON_EQ)) {
    // …原有 := 分支整体保持不变，末尾 endSimpleStmt 改为：
    if (terminate && !endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  // channel 发送语句：ch <- v（04 §8）
  if (!firstStar && check(p, TK_LARROW)) {
    if (!isAssignTarget(first)) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "invalid send target");
      return NULL;
    }
    advance(p);
    AstNode* value = parseExpr(p);
    if (value == NULL) {
      return NULL;
    }
    AstNode* node = astNewNode(p->arena, AST_SEND, line, col);
    node->as.sendStmt.chan = first;
    node->as.sendStmt.value = value;
    if (terminate && !endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  // …原有逗号列表/复合赋值/普通赋值/表达式语句分支保持不变（原局部变量 line/col 即参数；
  //    每处末尾的 if (!endSimpleStmt(p)) return NULL; 同样改为 terminate && 前缀）
}
```

`parseSimpleStmt` becomes:

```c
static AstNode* parseSimpleStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  bool firstStar = match(p, TK_STAR);  // 语句起始 * 仅解构目标合法
  AstNode* first = parseExpr(p);
  if (first == NULL) {
    return NULL;
  }
  return finishSimpleStmt(p, first, firstStar, line, col, true);
}
```

In `src/parser/ast.h`, add `sendStmt` union member (after `ifStmt`):

```c
    struct {
      AstNode* chan;
      AstNode* value;
    } sendStmt;  // AST_SEND：ch <- v
```

- [ ] **Step 5: Run tests**

Run: `cmake --build build --config Debug --target test_parser_complex; .\build\tests\Debug\test_parser_complex.exe`
Expected: `all tests passed`. Also run `.\build\tests\Debug\test_parser_expr.exe` and `test_parser_basic.exe` (refactor must not regress).

- [ ] **Step 6: Commit**

```bash
git add src/parser/ast.h src/parser/parser.c tests/unit/test_parser_complex.c tests/CMakeLists.txt
git commit -m "✨ feat(parser): parse recv/await/go prefix expressions and send statement"
```

### Task 2: Function declarations (defaults/rest/expr-body/`func*`/`async`) + yield

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.h`, `src/parser/parser.c`, `tests/unit/test_parser_complex.c`

- [ ] **Step 1: Write the failing tests** (append to `test_parser_complex.c`, register in `main` before the summary)

```c
// 首语句为函数声明时的快捷取用
static AstNode* func0(AstNode* mod) { return mod->as.list.items[0]; }

TEST(funcDeclFullForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f(a, b = 1, *r) -> a + b", &arena, &diags);
  AstNode* f = func0(mod);
  EXPECT_EQ(ctx, AST_FUNC_DECL, f->kind);
  EXPECT_EQ(ctx, 0, f->as.funcDecl.flags);
  EXPECT_EQ(ctx, 3, f->as.funcDecl.paramCount);
  EXPECT_EQ(ctx, false, f->as.funcDecl.params[0].rest);
  EXPECT_EQ(ctx, AST_INT, f->as.funcDecl.params[1].defaultExpr->kind);
  EXPECT_EQ(ctx, 1, (int)f->as.funcDecl.params[1].defaultExpr->as.intLit.v);
  EXPECT_EQ(ctx, true, f->as.funcDecl.params[2].rest);
  EXPECT_EQ(ctx, NULL, f->as.funcDecl.params[0].defaultExpr);
  EXPECT_EQ(ctx, NULL, f->as.funcDecl.body);
  EXPECT_EQ(ctx, AST_BINARY, f->as.funcDecl.exprBody->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(funcDeclGeneratorAndAsync) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func* g() { yield 1 }\nasync func h(u) { return await u() }", &arena, &diags);
  AstNode* g = func0(mod);
  EXPECT_EQ(ctx, AST_FUNC_DECL, g->kind);
  EXPECT_EQ(ctx, AST_FUNC_GENERATOR, g->as.funcDecl.flags);
  AstNode* y = g->as.funcDecl.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_YIELD, y->kind);
  EXPECT_EQ(ctx, AST_INT, y->as.exprStmt.expr->kind);
  AstNode* h = mod->as.list.items[1];
  EXPECT_EQ(ctx, AST_FUNC_ASYNC, h->as.funcDecl.flags);
  AstNode* r = h->as.funcDecl.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_RETURN, r->kind);
  EXPECT_EQ(ctx, AST_AWAIT, r->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(funcDeclDefaultString) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func greet(name, prefix = \"Mr.\") { return name }", &arena, &diags);
  AstNode* g = func0(mod);
  EXPECT_EQ(ctx, AST_STRING, g->as.funcDecl.params[1].defaultExpr->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errParamOrder) {
  const char* srcs[] = {"func f(a = 1, b) {}", "func f(*r, a) {}", "func f(*a, *b) {}"};
  for (int i = 0; i < 3; i++) {
    MsDiagSink diags = {0};
    AstArena arena;
    astArenaInit(&arena);
    parseSrc(srcs[i], &arena, &diags);
    EXPECT_EQ(ctx, 1, diags.count);
    EXPECT_EQ(ctx, MSE_SYN_PARAM_ORDER, diags.items[0].code);
    astArenaDestroy(&arena);
  }
}

TEST(funcRedefinitionAllowed) {
  // 05 §1：无重载，后定义覆盖先定义
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f() {}\nfunc f() { }", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(funcExprDefaultsRestExprBody) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var f = func(a, b = 2, *r) -> a + b\nvar g = func*(x) { yield x }", &arena, &diags);
  AstNode* f = mod->as.list.items[0]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_FUNC_EXPR, f->kind);
  EXPECT_EQ(ctx, 3, f->as.funcExpr.paramCount);
  EXPECT_EQ(ctx, AST_INT, f->as.funcExpr.params[1].defaultExpr->kind);
  EXPECT_EQ(ctx, true, f->as.funcExpr.params[2].rest);
  EXPECT_EQ(ctx, AST_BINARY, f->as.funcExpr.exprBody->kind);
  AstNode* g = mod->as.list.items[1]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_FUNC_GENERATOR, g->as.funcExpr.flags);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Run to verify failure** — `cmake --build build --config Debug --target test_parser_complex` fails (MSE_SYN_PARAM_ORDER / AST_FUNC_DECL payload missing).

- [ ] **Step 3: Implement**

`src/parser/parser.h`: add to `MseSyntaxCode` (after `MSE_SYN_SPREAD_NOT_LAST`):

```c
  MSE_SYN_PARAM_ORDER = 1012,    // 参数序：普通 → 默认 → 至多一个 *rest（05 §1）
```

Add `int32_t funcDepth;` to `struct Parser` (after `panic`), and `p->funcDepth = 0;` in `parserInit`.

`src/parser/ast.h`: flags (after the `AST_VAR_*` enum):

```c
// 函数声明/表达式形态标志（funcDecl/funcExpr.flags 位域）
enum {
  AST_FUNC_GENERATOR = 1,  // func*（05 §4）
  AST_FUNC_ASYNC = 2,      // async func（05 §8）
  AST_FUNC_STATIC = 4,     // 类体 static func（06 §4）
};
```

`funcExpr` union member gains `flags`/`exprBody` (replace existing member):

```c
    struct {
      ms_u8 flags;  // AST_FUNC_*
      AstParam* params;
      int32_t paramCount;
      AstNode* body;      // AST_BLOCK；表达式体 NULL
      AstNode* exprBody;  // -> expr（05 §1）
    } funcExpr;
```

New union member (after `funcExpr`):

```c
    struct {
      const char* name;
      int32_t nameLen;
      ms_u8 flags;  // AST_FUNC_*
      AstParam* params;
      int32_t paramCount;
      AstNode* body;      // AST_BLOCK；表达式体 NULL
      AstNode* exprBody;  // -> expr
    } funcDecl;  // AST_FUNC_DECL
```

Add `AST_YIELD` to `AstKind` (after `AST_CLASS_DECL`). New statement functions in `parser.c` (after `parseReturnStmt`):

```c
// throw / yield（04 §6、05 §4）：值可省（裸 throw 为重抛，07 §3）
static AstNode* parseValueJumpStmt(Parser* p, AstKind kind) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);
  AstNode* value = NULL;
  if (!check(p, TK_RBRACE) && !check(p, TK_SEMICOLON) && !check(p, TK_EOF) && p->cur.line == p->prevLine) {
    value = parseExprList(p);
    if (value == NULL) {
      return NULL;
    }
  }
  AstNode* node = astNewNode(p->arena, kind, line, col);
  node->as.exprStmt.expr = value;
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}

// 参数表（05 §1）：普通 → 默认 → 至多一个 *rest；违序 MSE1012；返回 false 已诊断
static bool parseParamList(Parser* p, AstParam** outParams, int32_t* outCount) {
  if (!match(p, TK_LPAREN)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '(' after function name");
    return false;
  }
  AstParam* params = NULL;
  int32_t count = 0, cap = 0;
  bool seenDefault = false;
  bool seenRest = false;
  while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
    bool rest = match(p, TK_STAR);
    if (!check(p, TK_ID)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected parameter name");
      goto fail;
    }
    AstParam param = {p->cur.start, p->cur.len, NULL, rest};
    advance(p);
    if (match(p, TK_EQ)) {
      if (rest) {
        errTok(p, &p->cur, MSE_SYN_PARAM_ORDER, "rest parameter cannot have a default");
        goto fail;
      }
      param.defaultExpr = parseExpr(p);
      if (param.defaultExpr == NULL) {
        goto fail;
      }
      seenDefault = true;
    }
    if (rest) {
      if (seenRest) {
        errTok(p, &p->cur, MSE_SYN_PARAM_ORDER, "at most one rest parameter allowed");
        goto fail;
      }
      seenRest = true;
    } else if (seenRest) {
      errTok(p, &p->cur, MSE_SYN_PARAM_ORDER, "no parameter allowed after rest parameter");
      goto fail;
    } else if (seenDefault && param.defaultExpr == NULL) {
      errTok(p, &p->cur, MSE_SYN_PARAM_ORDER, "parameter without default follows default parameter");
      goto fail;
    }
    MS_ARRAY_PUSH(params, count, cap, param);
    if (!match(p, TK_COMMA)) {
      break;
    }
  }
  if (!match(p, TK_RPAREN)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
    goto fail;
  }
  *outParams = params;
  *outCount = count;
  return true;
fail:
  free(params);
  return false;
}

// 函数体（05 §1）：{ } 块或 -> 表达式；新作用域 + funcDepth（defer 合法性，04 §7）
static bool parseFuncBody(Parser* p, AstNode** outBody, AstNode** outExprBody) {
  pushScope(p);
  p->funcDepth++;
  bool ok;
  if (match(p, TK_ARROW)) {
    *outBody = NULL;
    *outExprBody = parseExpr(p);
    ok = *outExprBody != NULL;
  } else {
    *outExprBody = NULL;
    *outBody = parseBlock(p);
    ok = *outBody != NULL;
  }
  p->funcDepth--;
  popScope(p);
  return ok;
}

// 函数声明（05 §1 §8）：[async] func[*] name(params) { } / -> expr
static AstNode* parseFuncDeclStmt(Parser* p, ms_u8 flags, int32_t line, int32_t col) {
  advance(p);  // func
  if (match(p, TK_STAR)) {
    flags |= AST_FUNC_GENERATOR;
  }
  if (!check(p, TK_ID)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected function name");
    return NULL;
  }
  const char* name = p->cur.start;
  int32_t nameLen = p->cur.len;
  advance(p);
  AstParam* params = NULL;
  int32_t paramCount = 0;
  if (!parseParamList(p, &params, &paramCount)) {
    return NULL;
  }
  AstNode* body = NULL;
  AstNode* exprBody = NULL;
  if (!parseFuncBody(p, &body, &exprBody)) {
    free(params);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_FUNC_DECL, line, col);
  node->as.funcDecl.name = name;
  node->as.funcDecl.nameLen = nameLen;
  node->as.funcDecl.flags = flags;
  node->as.funcDecl.paramCount = paramCount;
  node->as.funcDecl.params = astArenaAlloc(p->arena, sizeof(AstParam) * (size_t)paramCount);
  if (paramCount > 0) {
    memcpy(node->as.funcDecl.params, params, sizeof(AstParam) * (size_t)paramCount);
  }
  node->as.funcDecl.body = body;
  node->as.funcDecl.exprBody = exprBody;
  free(params);
  return node;
}
```

`parseFuncDeclStmt`/`parseFuncBody`/`parseParamList` need forward declarations next to the existing ones at the top of the statement section. Replace `parsePrimary`'s `TK_FUNC` case with:

```c
    case TK_FUNC: {  // 函数表达式（05 §2）：与声明共用参数/体解析
      int32_t line = p->cur.line, col = p->cur.col;
      advance(p);
      ms_u8 flags = 0;
      if (match(p, TK_STAR)) {
        flags |= AST_FUNC_GENERATOR;
      }
      AstParam* params = NULL;
      int32_t paramCount = 0;
      if (!parseParamList(p, &params, &paramCount)) {
        return NULL;
      }
      AstNode* body = NULL;
      AstNode* exprBody = NULL;
      if (!parseFuncBody(p, &body, &exprBody)) {
        free(params);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_FUNC_EXPR, line, col);
      node->as.funcExpr.flags = flags;
      node->as.funcExpr.paramCount = paramCount;
      node->as.funcExpr.params = astArenaAlloc(p->arena, sizeof(AstParam) * (size_t)paramCount);
      if (paramCount > 0) {
        memcpy(node->as.funcExpr.params, params, sizeof(AstParam) * (size_t)paramCount);
      }
      node->as.funcExpr.body = body;
      node->as.funcExpr.exprBody = exprBody;
      free(params);
      return node;
    }
```

In `parseStatement`, before the final `return parseSimpleStmt(p);`:

```c
  if (check(p, TK_THROW)) {
    return parseValueJumpStmt(p, AST_THROW);
  }
  if (check(p, TK_YIELD)) {
    return parseValueJumpStmt(p, AST_YIELD);
  }
  if (check(p, TK_FUNC)) {
    return parseFuncDeclStmt(p, 0, p->cur.line, p->cur.col);
  }
  if (check(p, TK_ASYNC)) {
    int32_t line = p->cur.line, col = p->cur.col;
    advance(p);
    if (!check(p, TK_FUNC)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'func' after 'async'");
      return NULL;
    }
    return parseFuncDeclStmt(p, AST_FUNC_ASYNC, line, col);
  }
```

- [ ] **Step 4: Run tests** — build, run `test_parser_complex.exe` + `test_parser_expr.exe` (funcExpr tests must still pass).
Expected: `all tests passed` both.

- [ ] **Step 5: Commit**

```bash
git add src/parser/ast.h src/parser/parser.h src/parser/parser.c tests/unit/test_parser_complex.c
git commit -m "✨ feat(parser): parse function declarations with defaults/rest/expr-body and yield"
```

### Task 3: for five forms + while

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.h`, `src/parser/parser.c`, `tests/unit/test_parser_complex.c`

- [ ] **Step 1: Write the failing tests**

```c
// 函数体内首语句（for 头 := 需函数作用域）
static AstNode* funcBody0(AstNode* mod) { return mod->as.list.items[0]->as.funcDecl.body->as.list.items[0]; }

TEST(forTriWithSugarPost) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f() { for i := 0; i < 10; i++ { s() } }", &arena, &diags);
  AstNode* f = funcBody0(mod);
  EXPECT_EQ(ctx, AST_FOR, f->kind);
  EXPECT_EQ(ctx, AST_FOR_TRI, f->as.forStmt.form);
  EXPECT_EQ(ctx, AST_VAR_DECL, f->as.forStmt.init->kind);
  EXPECT_EQ(ctx, AST_VAR_SHORT, f->as.forStmt.init->as.varDecl.flags);
  EXPECT_EQ(ctx, AST_BINARY, f->as.forStmt.cond->kind);
  EXPECT_EQ(ctx, AST_OP_LT, f->as.forStmt.cond->as.binary.op);
  EXPECT_EQ(ctx, AST_ASSIGN, f->as.forStmt.post->kind);
  EXPECT_EQ(ctx, AST_OP_ADD, f->as.forStmt.post->as.assign.op);
  EXPECT_EQ(ctx, 1, (int)f->as.forStmt.post->as.assign.value->as.intLit.v);
  EXPECT_EQ(ctx, AST_BLOCK, f->as.forStmt.body->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(forTriEmptySegments) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("for ; ; { f() }", &arena, &diags);
  AstNode* f = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_FOR_TRI, f->as.forStmt.form);
  EXPECT_EQ(ctx, NULL, f->as.forStmt.init);
  EXPECT_EQ(ctx, NULL, f->as.forStmt.cond);
  EXPECT_EQ(ctx, NULL, f->as.forStmt.post);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(forTriPartialSegments) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f() { for i := 0; ; { f() } }\nfor ; flag; { g() }", &arena, &diags);
  AstNode* a = funcBody0(mod);
  EXPECT_EQ(ctx, AST_FOR_TRI, a->as.forStmt.form);
  EXPECT_EQ(ctx, AST_VAR_DECL, a->as.forStmt.init->kind);
  EXPECT_EQ(ctx, NULL, a->as.forStmt.cond);
  EXPECT_EQ(ctx, NULL, a->as.forStmt.post);
  AstNode* b = mod->as.list.items[1];
  EXPECT_EQ(ctx, AST_FOR_TRI, b->as.forStmt.form);
  EXPECT_EQ(ctx, NULL, b->as.forStmt.init);
  EXPECT_EQ(ctx, AST_IDENT, b->as.forStmt.cond->kind);
  EXPECT_EQ(ctx, NULL, b->as.forStmt.post);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(forInForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("for x in xs { f(x) }\nfor k, v in m.items() { }", &arena, &diags);
  AstNode* a = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_FOR_IN, a->as.forStmt.form);
  EXPECT_EQ(ctx, AST_IDENT, a->as.forStmt.target->kind);
  EXPECT_EQ(ctx, AST_IDENT, a->as.forStmt.iter->kind);
  AstNode* b = mod->as.list.items[1];
  EXPECT_EQ(ctx, AST_FOR_IN, b->as.forStmt.form);
  EXPECT_EQ(ctx, AST_TUPLE, b->as.forStmt.target->kind);
  EXPECT_EQ(ctx, 2, b->as.forStmt.target->as.tuple.count);
  EXPECT_EQ(ctx, AST_CALL, b->as.forStmt.iter->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(forCondAndInfinite) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("for flag { }\nfor { }", &arena, &diags);
  EXPECT_EQ(ctx, AST_FOR_COND, mod->as.list.items[0]->as.forStmt.form);
  EXPECT_EQ(ctx, AST_IDENT, mod->as.list.items[0]->as.forStmt.cond->kind);
  EXPECT_EQ(ctx, AST_FOR_INF, mod->as.list.items[1]->as.forStmt.form);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(whileForm) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("while x < 3 { break }", &arena, &diags);
  AstNode* w = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_WHILE, w->kind);
  EXPECT_EQ(ctx, AST_BINARY, w->as.whileStmt.cond->kind);
  EXPECT_EQ(ctx, AST_BREAK, w->as.whileStmt.body->as.list.items[0]->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errForBadTarget) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("for 1 in xs { }", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_BAD_TARGET, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(forHeaderNoInBodyRestoredIn) {
  // 头部 in 留给迭代判别；体内 in 仍是运算符
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("for x in xs { if y in ys { } }", &arena, &diags);
  AstNode* f = mod->as.list.items[0];
  AstNode* ifStmt = f->as.forStmt.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_BINARY, ifStmt->as.ifStmt.cond->kind);
  EXPECT_EQ(ctx, AST_OP_IN, ifStmt->as.ifStmt.cond->as.binary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Run to verify failure** — compile error (`forStmt` payload / `AST_FOR_*` missing).

- [ ] **Step 3: Implement**

`src/parser/parser.h`: add `bool noIn;` to `struct Parser` (after `panic`), `p->noIn = false;` in `parserInit`.

`src/parser/ast.h`: forms enum (after `AST_COMP_*`):

```c
// for 形态（forStmt.form，04 §4）
enum {
  AST_FOR_TRI = 1,   // for init; cond; post（段均可省略）
  AST_FOR_IN = 2,    // for x in xs / for k, v in xs
  AST_FOR_COND = 3,  // for cond
  AST_FOR_INF = 4,   // for {}
};
```

Union member (after `sendStmt`):

```c
    struct {
      ms_u8 form;  // AST_FOR_*
      AstNode* init;    // TRI：简单语句（可 NULL）
      AstNode* cond;    // TRI/COND
      AstNode* post;    // TRI（i++/i-- 糖已展开为复合赋值）
      AstNode* target;  // IN：IDENT 或 TUPLE(IDENT)
      AstNode* iter;    // IN
      AstNode* body;    // AST_BLOCK
    } forStmt;
    struct {
      AstNode* cond;
      AstNode* body;
    } whileStmt;
```

`src/parser/parser.c`:

(a) `peekType` helper in the token-helper section (after `synchronize`):

```c
// 单 token 前瞻：快照推进再回滚（词法诊断屏蔽，由真实推进报告；仅用于判别，不产生副作用）
static MsTokenType peekType(Parser* p) {
  Scanner savedScan = p->scan;
  MsToken savedCur = p->cur;
  int32_t savedPrevLine = p->prevLine;
  p->scan.diags = NULL;
  MsToken next;
  scannerNext(&p->scan, &next);
  p->scan = savedScan;
  p->cur = savedCur;
  p->prevLine = savedPrevLine;
  return next.type;
}
```

(b) `parseCompare` first return condition becomes:

```c
  AstBinOp op = comparisonOpOf(p->cur.type);
  if (op == AST_OP_NONE || (op == AST_OP_IN && p->noIn)) {
    return lhs;  // noIn：for 头部顶层 in 留给迭代形态判别（04 §4）
  }
```

(c) for/while parsers (after `parseIfStmt`):

```c
// for 第三段（04 §4）：i++ / i-- 语法糖（语言无通用自增运算符，仅此处）→ 复合赋值
static AstNode* parseForPost(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  AstNode* first = parseExpr(p);
  if (first == NULL) {
    return NULL;
  }
  bool inc = check(p, TK_PLUS) && peekType(p) == TK_PLUS;
  bool dec = check(p, TK_MINUS) && peekType(p) == TK_MINUS;
  if (inc || dec) {
    advance(p);
    advance(p);
    if (!isAssignTarget(first)) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "invalid 'for' post target");
      return NULL;
    }
    AstNode* one = astNewNode(p->arena, AST_INT, line, col);
    one->as.intLit.v = 1;
    AstNode* node = astNewNode(p->arena, AST_ASSIGN, line, col);
    node->as.assign.target = first;
    node->as.assign.value = one;
    node->as.assign.op = inc ? AST_OP_ADD : AST_OP_SUB;
    return node;
  }
  return finishSimpleStmt(p, first, false, line, col, true);  // { 终止不消费，true 安全
}

// for 首段（04 §4）：不消费段尾 ';'，交由形态判别（';' 三段式 / in 迭代 / '{' 条件式）
static AstNode* parseForInit(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  bool firstStar = match(p, TK_STAR);
  AstNode* first = parseExpr(p);
  if (first == NULL) {
    return NULL;
  }
  return finishSimpleStmt(p, first, firstStar, line, col, false);
}

// 三段式后两段（04 §4）：init 可 NULL；cond/post 省略时第二 ';' 随 '{' 可省
static AstNode* parseForTriRest(Parser* p, AstNode* node, AstNode* init) {
  advance(p);  // 首段后的 ';'
  node->as.forStmt.init = init;
  if (!check(p, TK_SEMICOLON) && !check(p, TK_LBRACE)) {
    node->as.forStmt.cond = parseExpr(p);
    if (node->as.forStmt.cond == NULL) {
      return NULL;
    }
  }
  if (!match(p, TK_SEMICOLON) && !check(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ';' in 'for' header");
    return NULL;
  }
  if (!check(p, TK_LBRACE)) {
    node->as.forStmt.post = parseForPost(p);
    if (node->as.forStmt.post == NULL) {
      return NULL;
    }
  }
  node->as.forStmt.body = parseBlock(p);
  if (node->as.forStmt.body == NULL) {
    return NULL;
  }
  return node;
}

// for-in 目标：IDENT 或全 IDENT 的 TUPLE（04 §4）
static bool isIdentListTarget(const AstNode* n) {
  if (n->kind == AST_IDENT) {
    return true;
  }
  if (n->kind != AST_TUPLE) {
    return false;
  }
  for (int32_t i = 0; i < n->as.tuple.count; i++) {
    if (n->as.tuple.items[i]->kind != AST_IDENT) {
      return false;
    }
  }
  return true;
}

// for 五形态（04 §4）：判别见任务 §2（';' 三段式 / in 迭代 / '{' 无限 / 其余条件式）
static AstNode* parseForStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // for
  AstNode* node = astNewNode(p->arena, AST_FOR, line, col);
  node->as.forStmt.form = AST_FOR_TRI;
  if (check(p, TK_LBRACE)) {
    node->as.forStmt.form = AST_FOR_INF;
    node->as.forStmt.body = parseBlock(p);
    return node->as.forStmt.body != NULL ? node : NULL;
  }
  AstNode* init = NULL;
  if (!check(p, TK_SEMICOLON)) {  // 首段省略则直接三段式
    bool savedNoIn = p->noIn;
    p->noIn = true;
    init = parseForInit(p);  // 不消费段尾 ';'（判别依赖）
    p->noIn = savedNoIn;
    if (init == NULL) {
      return NULL;
    }
  }
  if (check(p, TK_IN)) {  // 迭代形态：目标取自首段表达式
    advance(p);
    AstNode* target = init->kind == AST_EXPR_STMT ? init->as.exprStmt.expr : NULL;
    if (target == NULL || !isIdentListTarget(target)) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "'for' target must be an identifier list");
      return NULL;
    }
    node->as.forStmt.form = AST_FOR_IN;
    node->as.forStmt.target = target;
    node->as.forStmt.iter = parseExpr(p);
    if (node->as.forStmt.iter == NULL) {
      return NULL;
    }
    node->as.forStmt.body = parseBlock(p);
    return node->as.forStmt.body != NULL ? node : NULL;
  }
  if (init != NULL && check(p, TK_LBRACE)) {  // 条件形态：首段即条件
    if (init->kind != AST_EXPR_STMT) {
      errTok(p, &p->cur, MSE_SYN_UNEXPECTED, "expected expression as 'for' condition");
      return NULL;
    }
    node->as.forStmt.form = AST_FOR_COND;
    node->as.forStmt.cond = init->as.exprStmt.expr;
    node->as.forStmt.body = parseBlock(p);
    return node->as.forStmt.body != NULL ? node : NULL;
  }
  if (!check(p, TK_SEMICOLON)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ';', 'in' or '{' in 'for' header");
    return NULL;
  }
  return parseForTriRest(p, node, init);
}

// while（04 §4）
static AstNode* parseWhileStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);
  AstNode* cond = parseExpr(p);
  if (cond == NULL) {
    return NULL;
  }
  AstNode* body = parseBlock(p);
  if (body == NULL) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_WHILE, line, col);
  node->as.whileStmt.cond = cond;
  node->as.whileStmt.body = body;
  return node;
}
```

`parseForStmt`/`parseWhileStmt` also need forward declarations before use. `parseForPost` uses `isAssignTarget`/`finishSimpleStmt` — place these after `finishSimpleStmt` (i.e., after the old `parseSimpleStmt` block) and before `parseStatement`. Hook into `parseStatement` (break/continue 合法性检查归 T009，04 §6):

```c
  if (check(p, TK_FOR)) {
    return parseForStmt(p);
  }
  if (check(p, TK_WHILE)) {
    return parseWhileStmt(p);
  }
  if (check(p, TK_BREAK) || check(p, TK_CONTINUE)) {
    AstKind kind = check(p, TK_BREAK) ? AST_BREAK : AST_CONTINUE;
    int32_t line = p->cur.line, col = p->cur.col;
    advance(p);
    if (!endSimpleStmt(p)) {
      return NULL;
    }
    return astNewNode(p->arena, kind, line, col);
  }
```

- [ ] **Step 4: Run tests** — `test_parser_complex.exe` + full parser trio.
- [ ] **Step 5: Commit**

```bash
git add src/parser/ast.h src/parser/parser.h src/parser/parser.c tests/unit/test_parser_complex.c
git commit -m "✨ feat(parser): parse for five forms, while, break and continue"
```

### Task 4: throw (incl. bare rethrow) — covered by parseValueJumpStmt from Task 2

**Files:** none new — Task 2 already delivered `AST_THROW` via `parseValueJumpStmt`. This task only adds its dedicated tests.

- [ ] **Step 1: Write the failing tests**

```c
TEST(throwForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("throw ValueError(\"bad input: ${x}\")", &arena, &diags);
  AstNode* t = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_THROW, t->kind);
  EXPECT_EQ(ctx, AST_CALL, t->as.exprStmt.expr->kind);
  diags = (MsDiagSink){0};
  mod = parseSrc("func f() { try { g() } catch e { throw } }", &arena, &diags);
  AstNode* catchBody = mod->as.list.items[0]->as.funcDecl.body->as.list.items[0]->as.tryStmt.catches[0].block;
  AstNode* bare = catchBody->as.list.items[0];
  EXPECT_EQ(ctx, AST_THROW, bare->kind);
  EXPECT_EQ(ctx, NULL, bare->as.exprStmt.expr);  // 裸 throw 重抛（07 §3）——注意：依赖 Task 6 的 try 解析，此断言在 Task 6 后生效
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
```

Note: the bare-throw half needs `try` (Task 6); keep only the first half of this test until then — add the second half when Task 6 lands (plan step in Task 6).

- [ ] **Step 2/3: Verify green** (already implemented in Task 2) — build + run.
- [ ] **Step 4: Commit**

```bash
git add tests/unit/test_parser_complex.c
git commit -m "✅ test(parser): cover throw statement forms"
```

### Task 5: switch (value / type)

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.c`, `tests/unit/test_parser_complex.c`

- [ ] **Step 1: Write the failing tests**

```c
TEST(switchValueForm) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("switch x {\ncase 1, 2: f()\ncase 3: g()\ndefault: h()\n}", &arena, &diags);
  AstNode* sw = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_SWITCH, sw->kind);
  EXPECT_EQ(ctx, false, sw->as.switchStmt.isType);
  EXPECT_EQ(ctx, AST_IDENT, sw->as.switchStmt.subject->kind);
  EXPECT_EQ(ctx, 3, sw->as.switchStmt.count);
  AstNode* c0 = sw->as.switchStmt.cases[0];
  EXPECT_EQ(ctx, AST_SWITCH_CASE, c0->kind);
  EXPECT_EQ(ctx, false, c0->as.switchCase.isDefault);
  EXPECT_EQ(ctx, 2, c0->as.switchCase.valueCount);
  EXPECT_EQ(ctx, AST_INT, c0->as.switchCase.values[1]->kind);
  EXPECT_EQ(ctx, AST_EXPR_STMT, c0->as.switchCase.body->kind);
  AstNode* c2 = sw->as.switchStmt.cases[2];
  EXPECT_EQ(ctx, true, c2->as.switchCase.isDefault);
  EXPECT_EQ(ctx, 0, c2->as.switchCase.valueCount);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(switchTypeFormWithAs) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("switch type(v) {\ncase string as s: print(len(s))\ncase int, bigint: f()\ndefault: g()\n}",
                          &arena, &diags);
  AstNode* sw = mod->as.list.items[0];
  EXPECT_EQ(ctx, true, sw->as.switchStmt.isType);
  EXPECT_EQ(ctx, AST_IDENT, sw->as.switchStmt.subject->kind);
  AstNode* c0 = sw->as.switchStmt.cases[0];
  EXPECT_EQ(ctx, 1, c0->as.switchCase.valueCount);
  EXPECT_EQ(ctx, AST_IDENT, c0->as.switchCase.values[0]->kind);
  EXPECT_STREQ(ctx, "s", strOf(c0->as.switchCase.bindName, c0->as.switchCase.bindLen));
  AstNode* c1 = sw->as.switchStmt.cases[1];
  EXPECT_EQ(ctx, 2, c1->as.switchCase.valueCount);
  EXPECT_EQ(ctx, NULL, c1->as.switchCase.bindName);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(switchSubjectNamedTypeStaysValue) {
  // type 是普通标识符：switch type { … } 为值式（任务 §6）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("switch type {\ncase 1: f()\n}", &arena, &diags);
  AstNode* sw = mod->as.list.items[0];
  EXPECT_EQ(ctx, false, sw->as.switchStmt.isType);
  EXPECT_EQ(ctx, AST_IDENT, sw->as.switchStmt.subject->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errSwitchMissingColon) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("switch x {\ncase 1 f()\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  astArenaDestroy(&arena);
}
```

The tests use a small NUL-terminating helper for borrowed name slices (add next to `expr0`):

```c
// 借用源文本的非 NUL 结尾名 → 静态缓冲（单断言内使用，勿跨用例持有）
static const char* strOf(const char* start, int32_t len) {
  static char buf[64];
  memcpy(buf, start, (size_t)(len < 63 ? len : 63));
  buf[len < 63 ? len : 63] = '\0';
  return buf;
}
```

- [ ] **Step 2: Run to verify failure** — compile error (`switchStmt` payload missing).

- [ ] **Step 3: Implement**

`src/parser/ast.h` union members (after `whileStmt`):

```c
    struct {
      bool isType;       // 值式 false / 类型式 true（04 §5）
      AstNode* subject;  // 值式：判别表达式；类型式：type(v) 的 v
      AstNode** cases;   // AST_SWITCH_CASE 列表
      int32_t count;
    } switchStmt;
    struct {
      bool isDefault;
      AstNode** values;  // 值式：判别值；类型式：类型名 AST_IDENT（04 §5.2）
      int32_t valueCount;
      const char* bindName;  // 类型式 as 绑定；NULL 无
      int32_t bindLen;
      AstNode* body;  // 块或单语句（08 §3 同则）
    } switchCase;
```

`src/parser/parser.c` (after `parseWhileStmt`, forward-declared):

```c
// switch 值式/类型式（04 §5）：type 为普通标识符，switch 后 ID "type"+peek( 特判（任务 §6）
static AstNode* parseSwitchStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // switch
  bool isType = check(p, TK_ID) && p->cur.len == 4 && memcmp(p->cur.start, "type", 4) == 0 &&
                peekType(p) == TK_LPAREN;
  AstNode* subject;
  if (isType) {
    advance(p);  // type
    advance(p);  // (
    subject = parseExpr(p);
    if (subject == NULL) {
      return NULL;
    }
    if (!match(p, TK_RPAREN)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')' after 'type('");
      return NULL;
    }
  } else {
    subject = parseExpr(p);
    if (subject == NULL) {
      return NULL;
    }
  }
  if (!match(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{' after switch subject");
    return NULL;
  }
  AstNode** cases = NULL;
  int32_t count = 0, cap = 0;
  while (check(p, TK_CASE) || check(p, TK_DEFAULT)) {
    int32_t caseLine = p->cur.line, caseCol = p->cur.col;
    bool isDefault = check(p, TK_DEFAULT);
    advance(p);
    AstNode** values = NULL;
    int32_t valueCount = 0, valueCap = 0;
    const char* bindName = NULL;
    int32_t bindLen = 0;
    bool ok = true;
    if (!isDefault) {
      for (;;) {
        AstNode* value;
        if (isType) {
          if (!check(p, TK_ID)) {
            errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected type name");
            ok = false;
            break;
          }
          value = astNewNode(p->arena, AST_IDENT, p->cur.line, p->cur.col);
          value->as.ident.name = p->cur.start;
          value->as.ident.nameLen = p->cur.len;
          advance(p);
        } else {
          value = parseExpr(p);
          if (value == NULL) {
            ok = false;
            break;
          }
        }
        MS_ARRAY_PUSH(values, valueCount, valueCap, value);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
      if (ok && isType && match(p, TK_AS)) {  // case X as p：绑定进块作用域（04 §5.2）
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected identifier after 'as'");
          ok = false;
        } else {
          bindName = p->cur.start;
          bindLen = p->cur.len;
          advance(p);
        }
      }
    }
    if (ok && !match(p, TK_COLON)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ':' after case");
      ok = false;
    }
    AstNode* body = NULL;
    if (ok) {
      body = parseStatement(p);  // case 体为块或单语句
      if (body == NULL) {
        ok = false;
      }
    }
    if (!ok) {
      free(values);
      free(cases);
      return NULL;
    }
    AstNode* caseNode = astNewNode(p->arena, AST_SWITCH_CASE, caseLine, caseCol);
    caseNode->as.switchCase.isDefault = isDefault;
    caseNode->as.switchCase.valueCount = valueCount;
    caseNode->as.switchCase.values = astNewNodeArray(p->arena, valueCount);
    if (valueCount > 0) {
      memcpy(caseNode->as.switchCase.values, values, sizeof(AstNode*) * (size_t)valueCount);
    }
    caseNode->as.switchCase.bindName = bindName;
    caseNode->as.switchCase.bindLen = bindLen;
    caseNode->as.switchCase.body = body;
    free(values);
    MS_ARRAY_PUSH(cases, count, cap, caseNode);
  }
  if (!match(p, TK_RBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'case' or '}' in switch body");
    free(cases);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_SWITCH, line, col);
  node->as.switchStmt.isType = isType;
  node->as.switchStmt.subject = subject;
  node->as.switchStmt.count = count;
  node->as.switchStmt.cases = astNewNodeArray(p->arena, count);
  if (count > 0) {
    memcpy(node->as.switchStmt.cases, cases, sizeof(AstNode*) * (size_t)count);
  }
  free(cases);
  return node;
}
```

Hook: `if (check(p, TK_SWITCH)) { return parseSwitchStmt(p); }` in `parseStatement`.

- [ ] **Step 4: Run tests**
- [ ] **Step 5: Commit**

```bash
git add src/parser/ast.h src/parser/parser.c tests/unit/test_parser_complex.c
git commit -m "✨ feat(parser): parse value and type switch with as binding"
```

### Task 6: try / catch / finally

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.h`, `src/parser/parser.c`, `tests/unit/test_parser_complex.c`

- [ ] **Step 1: Write the failing tests**

```c
TEST(tryCatchFinallyForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("try {\nrisky(x)\n} catch A, B {\ng()\n} catch e {\nprint(e)\n} finally {\nh()\n}", &arena, &diags);
  AstNode* t = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_TRY, t->kind);
  EXPECT_EQ(ctx, 1, t->as.tryStmt.tryBlock->as.list.count);
  EXPECT_EQ(ctx, 2, t->as.tryStmt.catchCount);
  EXPECT_EQ(ctx, 2, t->as.tryStmt.catches[0].typeCount);
  EXPECT_EQ(ctx, NULL, t->as.tryStmt.catches[0].bindName);
  EXPECT_STREQ(ctx, "e", strOf(t->as.tryStmt.catches[1].bindName, t->as.tryStmt.catches[1].bindLen));
  EXPECT_EQ(ctx, 0, t->as.tryStmt.catches[1].typeCount);
  EXPECT_EQ(ctx, AST_BLOCK, t->as.tryStmt.finallyBlock->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errCatchOrder) {
  // 裸绑定兜底最后（07 §1）：裸子句之后再出现任何 catch 均报错
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("try{}catch e{}catch V{}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_CATCH_ORDER, diags.items[0].code);
  EXPECT_EQ(ctx, 1, diags.items[0].line);
  astArenaDestroy(&arena);
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("try{}catch e{}catch A, B{}", &arena, &diags);
  EXPECT_EQ(ctx, MSE_SYN_CATCH_ORDER, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errTryWithoutCatchFinally) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("try {\nf()\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  astArenaDestroy(&arena);
}
```

Also extend `throwForms` (Task 4) with the bare-rethrow half (uncomment/add):

```c
  diags = (MsDiagSink){0};
  mod = parseSrc("func f() { try { g() } catch e { throw } }", &arena, &diags);
  AstNode* tryStmt = mod->as.list.items[0]->as.funcDecl.body->as.list.items[0];
  AstNode* bare = tryStmt->as.tryStmt.catches[0].block->as.list.items[0];
  EXPECT_EQ(ctx, AST_THROW, bare->kind);
  EXPECT_EQ(ctx, NULL, bare->as.exprStmt.expr);
  EXPECT_EQ(ctx, 0, diags.count);
```

- [ ] **Step 2: Run to verify failure** — compile error (`tryStmt` payload / `AstCatchClause` / `MSE_SYN_CATCH_ORDER` missing).

- [ ] **Step 3: Implement**

`src/parser/parser.h`: `MSE_SYN_CATCH_ORDER = 1011` (before `MSE_SYN_PARAM_ORDER`, keep numeric order).

`src/parser/ast.h`: struct before `AstNode` (after `AstStrSeg`):

```c
// catch 子句（tryStmt.catches 元素，07 §1）：types 空 = 裸绑定兜底
typedef struct AstCatchClause {
  AstNode** types;  // AST_IDENT 类型清单
  int32_t typeCount;
  const char* bindName;  // 可选绑定名
  int32_t bindLen;
  AstNode* block;  // AST_BLOCK
} AstCatchClause;
```

Union member (after `switchCase`):

```c
    struct {
      AstNode* tryBlock;
      AstCatchClause* catches;
      int32_t catchCount;
      AstNode* finallyBlock;  // 可 NULL
    } tryStmt;
```

Add `AST_TRY` to `AstKind` (after `AST_CLASS_DECL`). `src/parser/parser.c` (after `parseSwitchStmt`):

```c
// try/catch/finally（07 §1）：类型子句在前、裸绑定兜底最后
// 单 ID 子句按裸绑定解析（任务 §5.1 排序用例；类型子句省略绑定的复核归 T023 binder）
static AstNode* parseTryStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // try
  AstNode* tryBlock = parseBlock(p);
  if (tryBlock == NULL) {
    return NULL;
  }
  AstCatchClause* catches = NULL;
  int32_t catchCount = 0, catchCap = 0;
  bool seenBare = false;
  bool ok = true;
  while (check(p, TK_CATCH)) {
    int32_t catchLine = p->cur.line, catchCol = p->cur.col, catchLen = p->cur.len;
    advance(p);
    if (seenBare) {  // 裸绑定必须兜底最后
      errPos(p, catchLine, catchCol, catchLen, MSE_SYN_CATCH_ORDER, "catch-all clause must be the last catch");
      ok = false;
      break;
    }
    AstCatchClause clause = {0};
    if (check(p, TK_ID)) {
      const char* firstName = p->cur.start;
      int32_t firstLen = p->cur.len;
      int32_t nameLine = p->cur.line, nameCol = p->cur.col;
      advance(p);
      if (check(p, TK_COMMA)) {  // 类型子句：catch A, B, …
        AstNode** types = NULL;
        int32_t typeCount = 0, typeCap = 0;
        AstNode* firstType = astNewNode(p->arena, AST_IDENT, nameLine, nameCol);
        firstType->as.ident.name = firstName;
        firstType->as.ident.nameLen = firstLen;
        MS_ARRAY_PUSH(types, typeCount, typeCap, firstType);
        while (match(p, TK_COMMA)) {
          if (!check(p, TK_ID)) {
            errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected exception type name");
            ok = false;
            break;
          }
          AstNode* t = astNewNode(p->arena, AST_IDENT, p->cur.line, p->cur.col);
          t->as.ident.name = p->cur.start;
          t->as.ident.nameLen = p->cur.len;
          advance(p);
          MS_ARRAY_PUSH(types, typeCount, typeCap, t);
        }
        if (!ok) {
          free(types);
          break;
        }
        clause.typeCount = typeCount;
        clause.types = astNewNodeArray(p->arena, typeCount);
        if (typeCount > 0) {
          memcpy(clause.types, types, sizeof(AstNode*) * (size_t)typeCount);
        }
        free(types);
      } else {  // 裸绑定兜底：catch e
        clause.bindName = firstName;
        clause.bindLen = firstLen;
        seenBare = true;
      }
    }
    clause.block = parseBlock(p);
    if (clause.block == NULL) {
      free(clause.types);
      ok = false;
      break;
    }
    MS_ARRAY_PUSH(catches, catchCount, catchCap, clause);
  }
  AstNode* finallyBlock = NULL;
  if (ok && match(p, TK_FINALLY)) {
    finallyBlock = parseBlock(p);
    if (finallyBlock == NULL) {
      ok = false;
    }
  }
  if (ok && catchCount == 0 && finallyBlock == NULL) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'catch' or 'finally' after try block");
    ok = false;
  }
  if (!ok) {
    for (int32_t i = 0; i < catchCount; i++) {
      // catches[].types 取自 arena，无需逐项释放
    }
    free(catches);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_TRY, line, col);
  node->as.tryStmt.tryBlock = tryBlock;
  node->as.tryStmt.catchCount = catchCount;
  node->as.tryStmt.catches = astArenaAlloc(p->arena, sizeof(AstCatchClause) * (size_t)catchCount);
  if (catchCount > 0) {
    memcpy(node->as.tryStmt.catches, catches, sizeof(AstCatchClause) * (size_t)catchCount);
  }
  free(catches);
  node->as.tryStmt.finallyBlock = finallyBlock;
  return node;
}
```

(The empty `for` in the failure path is dead — just `free(catches); return NULL;`; shown here so the implementer drops it.) Hook: `if (check(p, TK_TRY)) { return parseTryStmt(p); }`.

- [ ] **Step 4: Run tests**
- [ ] **Step 5: Commit**

```bash
git add src/parser/ast.h src/parser/parser.h src/parser/parser.c tests/unit/test_parser_complex.c
git commit -m "✨ feat(parser): parse try/catch/finally with clause ordering check"
```

### Task 7: defer

**Files:**
- Modify: `src/parser/parser.h`, `src/parser/parser.c`, `tests/unit/test_parser_complex.c`

- [ ] **Step 1: Write the failing tests**

```c
TEST(deferRegistersCall) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func copyFile(src, dst) {\nvar f = open(src)\ndefer f.close()\nreturn open(dst)\n}", &arena, &diags);
  AstNode* body = mod->as.list.items[0]->as.funcDecl.body;
  EXPECT_EQ(ctx, 3, body->as.list.count);
  AstNode* d = body->as.list.items[1];
  EXPECT_EQ(ctx, AST_DEFER, d->kind);
  EXPECT_EQ(ctx, AST_CALL, d->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, AST_MEMBER, d->as.exprStmt.expr->as.call.callee->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errDeferAtModuleLevel) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("defer f.close()", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DEFER_TOPLEVEL, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errDeferNotCall) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("func f() {\ndefer x\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DEFER_CALL, diags.items[0].code);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Run to verify failure** — compile error (`MSE_SYN_DEFER_*` missing).

- [ ] **Step 3: Implement**

`src/parser/parser.h`: `MSE_SYN_DEFER_TOPLEVEL = 1013`, `MSE_SYN_DEFER_CALL = 1014`.

`src/parser/parser.c` (after `parseTryStmt`):

```c
// defer（04 §7）：仅函数体内、操作数为调用（funcDepth 由 parseFuncBody 维护）
static AstNode* parseDeferStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // defer
  if (p->funcDepth == 0) {
    errTok(p, &p->cur, MSE_SYN_DEFER_TOPLEVEL, "'defer' is only allowed inside a function");
    return NULL;
  }
  AstNode* call = parseExpr(p);
  if (call == NULL) {
    return NULL;
  }
  if (call->kind != AST_CALL) {
    errPos(p, line, col, 5, MSE_SYN_DEFER_CALL, "'defer' requires a call expression");
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_DEFER, line, col);
  node->as.exprStmt.expr = call;
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}
```

Hook: `if (check(p, TK_DEFER)) { return parseDeferStmt(p); }`.

- [ ] **Step 4: Run tests**
- [ ] **Step 5: Commit**

```bash
git add src/parser/parser.h src/parser/parser.c tests/unit/test_parser_complex.c
git commit -m "✨ feat(parser): parse defer with function-scope and call checks"
```

### Task 8: select

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.c`, `tests/unit/test_parser_complex.c`

- [ ] **Step 1: Write the failing tests**

```c
TEST(selectThreeBranchForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("select {\ncase v = <-ch1: print(v)\ncase ch2 <- x: send()\ncase <-tick: tock()\ndefault: none()\n}",
                          &arena, &diags);
  AstNode* sel = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_SELECT, sel->kind);
  EXPECT_EQ(ctx, 4, sel->as.selectStmt.count);
  AstNode* c0 = sel->as.selectStmt.cases[0];
  EXPECT_EQ(ctx, AST_SELECT_CASE, c0->kind);
  EXPECT_EQ(ctx, AST_SEL_RECV, c0->as.selectCase.form);
  EXPECT_EQ(ctx, AST_IDENT, c0->as.selectCase.target->kind);
  EXPECT_EQ(ctx, AST_RECV, c0->as.selectCase.chan->kind);
  AstNode* c1 = sel->as.selectStmt.cases[1];
  EXPECT_EQ(ctx, AST_SEL_SEND, c1->as.selectCase.form);
  EXPECT_EQ(ctx, AST_IDENT, c1->as.selectCase.chan->kind);
  EXPECT_EQ(ctx, AST_IDENT, c1->as.selectCase.value->kind);
  EXPECT_EQ(ctx, NULL, c1->as.selectCase.target);
  AstNode* c2 = sel->as.selectStmt.cases[2];
  EXPECT_EQ(ctx, AST_SEL_RECV, c2->as.selectCase.form);
  EXPECT_EQ(ctx, NULL, c2->as.selectCase.target);
  EXPECT_EQ(ctx, AST_RECV, c2->as.selectCase.chan->kind);
  AstNode* c3 = sel->as.selectStmt.cases[3];
  EXPECT_EQ(ctx, AST_SEL_DEFAULT, c3->as.selectCase.form);
  EXPECT_EQ(ctx, AST_EXPR_STMT, c3->as.selectCase.body->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Run to verify failure** — compile error (`selectStmt` payload missing).

- [ ] **Step 3: Implement**

`src/parser/ast.h`: forms (after `AST_FOR_*`):

```c
// select 分支形态（selectCase.form，04 §8）
enum {
  AST_SEL_RECV = 1,    // case v = <-ch: / case <-ch:
  AST_SEL_SEND = 2,    // case ch <- v:
  AST_SEL_DEFAULT = 3, // default:
};
```

Union members (after `tryStmt`):

```c
    struct {
      AstNode** cases;  // AST_SELECT_CASE 列表
      int32_t count;
    } selectStmt;
    struct {
      ms_u8 form;       // AST_SEL_*
      AstNode* target;  // RECV：赋值目标（NULL = 丢弃接收）
      AstNode* chan;    // RECV：AST_RECV 源；SEND：目标 channel
      AstNode* value;   // SEND：发送值
      AstNode* body;    // 块或单语句
    } selectCase;
```

`src/parser/parser.c` (after `parseDeferStmt`):

```c
// select（04 §8、08 §3）：接收赋值 / 丢弃接收 / 发送 / default
static AstNode* parseSelectStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // select
  if (!match(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{' after 'select'");
    return NULL;
  }
  AstNode** cases = NULL;
  int32_t count = 0, cap = 0;
  while (check(p, TK_CASE) || check(p, TK_DEFAULT)) {
    int32_t caseLine = p->cur.line, caseCol = p->cur.col;
    bool isDefault = check(p, TK_DEFAULT);
    advance(p);
    ms_u8 form = AST_SEL_DEFAULT;
    AstNode* target = NULL;
    AstNode* chan = NULL;
    AstNode* value = NULL;
    if (!isDefault) {
      AstNode* first = parseExpr(p);
      if (first == NULL) {
        free(cases);
        return NULL;
      }
      if (first->kind == AST_RECV) {  // case <-ch:
        form = AST_SEL_RECV;
        chan = first;
      } else if (check(p, TK_LARROW)) {  // case ch <- v:
        form = AST_SEL_SEND;
        chan = first;
        advance(p);
        value = parseExpr(p);
        if (value == NULL) {
          free(cases);
          return NULL;
        }
      } else if (match(p, TK_EQ)) {  // case v = <-ch:
        form = AST_SEL_RECV;
        if (!isAssignTarget(first)) {
          errPos(p, first->line, first->col, 1, MSE_SYN_BAD_TARGET, "invalid receive target");
          free(cases);
          return NULL;
        }
        if (!check(p, TK_LARROW)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '<-' in select receive case");
          free(cases);
          return NULL;
        }
        target = first;
        chan = parseExpr(p);  // 以 <- 起始 → AST_RECV
        if (chan == NULL) {
          free(cases);
          return NULL;
        }
      } else {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '=' or '<-' in select case");
        free(cases);
        return NULL;
      }
    }
    if (!match(p, TK_COLON)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ':' after select case");
      free(cases);
      return NULL;
    }
    AstNode* body = parseStatement(p);
    if (body == NULL) {
      free(cases);
      return NULL;
    }
    AstNode* caseNode = astNewNode(p->arena, AST_SELECT_CASE, caseLine, caseCol);
    caseNode->as.selectCase.form = form;
    caseNode->as.selectCase.target = target;
    caseNode->as.selectCase.chan = chan;
    caseNode->as.selectCase.value = value;
    caseNode->as.selectCase.body = body;
    MS_ARRAY_PUSH(cases, count, cap, caseNode);
  }
  if (!match(p, TK_RBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'case' or '}' in select body");
    free(cases);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_SELECT, line, col);
  node->as.selectStmt.count = count;
  node->as.selectStmt.cases = astNewNodeArray(p->arena, count);
  if (count > 0) {
    memcpy(node->as.selectStmt.cases, cases, sizeof(AstNode*) * (size_t)count);
  }
  free(cases);
  return node;
}
```

Hook: `if (check(p, TK_SELECT)) { return parseSelectStmt(p); }`.

- [ ] **Step 4: Run tests**
- [ ] **Step 5: Commit**

```bash
git add src/parser/ast.h src/parser/parser.c tests/unit/test_parser_complex.c
git commit -m "✨ feat(parser): parse select with recv/send/default branches"
```

### Task 9: import (3 forms)

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.h`, `src/parser/parser.c`, `tests/unit/test_parser_complex.c`

- [ ] **Step 1: Write the failing tests**

```c
TEST(importForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("import \"strutil\"\nimport \"net/addr\" as netaddr\nfrom \"strutil\" import { join, split as sp, pad }\nfrom \"strutil\" import *",
                          &arena, &diags);
  EXPECT_EQ(ctx, 4, mod->as.list.count);
  AstNode* a = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_IMPORT, a->kind);
  EXPECT_STREQ(ctx, "strutil", strOf(a->as.importStmt.path, a->as.importStmt.pathLen));
  EXPECT_EQ(ctx, NULL, a->as.importStmt.alias);
  AstNode* b = mod->as.list.items[1];
  EXPECT_STREQ(ctx, "net/addr", strOf(b->as.importStmt.path, b->as.importStmt.pathLen));
  EXPECT_STREQ(ctx, "netaddr", strOf(b->as.importStmt.alias, b->as.importStmt.aliasLen));
  AstNode* c = mod->as.list.items[2];
  EXPECT_EQ(ctx, 3, c->as.importStmt.nameCount);
  EXPECT_EQ(ctx, false, c->as.importStmt.star);
  EXPECT_STREQ(ctx, "join", strOf(c->as.importStmt.names[0].name, c->as.importStmt.names[0].nameLen));
  EXPECT_EQ(ctx, NULL, c->as.importStmt.names[0].asName);
  EXPECT_STREQ(ctx, "sp", strOf(c->as.importStmt.names[1].asName, c->as.importStmt.names[1].asLen));
  AstNode* d = mod->as.list.items[3];
  EXPECT_EQ(ctx, true, d->as.importStmt.star);
  EXPECT_EQ(ctx, 0, d->as.importStmt.nameCount);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errImportPathNotLiteral) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("import os", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_IMPORT_PATH, diags.items[0].code);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Run to verify failure** — compile error (`importStmt` payload missing).

- [ ] **Step 3: Implement**

`src/parser/parser.h`: `MSE_SYN_IMPORT_PATH = 1015`.

`src/parser/ast.h`: struct (after `AstCatchClause`):

```c
// from-import 具名项（importStmt.names 元素，09 §2）
typedef struct AstImportName {
  const char* name;
  int32_t nameLen;
  const char* asName;  // b as c 本地名；NULL = name
  int32_t asLen;
} AstImportName;
```

Union member (after `selectCase`):

```c
    struct {
      const char* path;     // 模块路径（字面量原文，引号/反引号已剥离）
      int32_t pathLen;
      const char* alias;    // import "m" as x；无别名 NULL
      int32_t aliasLen;
      AstImportName* names; // from-import 具名清单；非 from 形式 NULL
      int32_t nameCount;
      bool star;            // from "m" import *
    } importStmt;
```

`src/parser/parser.c` (after `parseSelectStmt`):

```c
// import 路径：普通/原始字符串字面量；插值与多段拒绝（09 §2 动态加载走 importlib）
static bool parseImportPath(Parser* p, const char** outStart, int32_t* outLen) {
  if (check(p, TK_RAW_STR)) {
    *outStart = p->cur.start + (p->cur.len >= 2 ? 1 : 0);
    *outLen = p->cur.len >= 2 ? p->cur.len - 2 : 0;
    advance(p);
    return true;
  }
  if (!check(p, TK_STRING)) {
    errTok(p, &p->cur, MSE_SYN_IMPORT_PATH, "import path must be a string literal");
    return false;
  }
  AstNode* lit = parseStringLit(p);
  if (lit == NULL) {
    return false;
  }
  if (lit->as.strLit.count != 1 || lit->as.strLit.segs[0].expr != NULL) {
    errPos(p, lit->line, lit->col, 1, MSE_SYN_IMPORT_PATH, "import path must be a plain string literal");
    return false;
  }
  *outStart = lit->as.strLit.segs[0].start;
  *outLen = lit->as.strLit.segs[0].len;
  return true;
}

// import 三式（04 §9、09 §2）：import "m" [as x] / from "m" import {…|*}
static AstNode* parseImportStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  bool from = check(p, TK_FROM);
  advance(p);  // import / from
  const char* path = NULL;
  int32_t pathLen = 0;
  if (!parseImportPath(p, &path, &pathLen)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_IMPORT, line, col);
  node->as.importStmt.path = path;
  node->as.importStmt.pathLen = pathLen;
  if (from) {
    if (!match(p, TK_IMPORT)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'import' after module path");
      return NULL;
    }
    if (match(p, TK_STAR)) {
      node->as.importStmt.star = true;
    } else {
      if (!match(p, TK_LBRACE)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{' or '*' in from-import");
        return NULL;
      }
      AstImportName* names = NULL;
      int32_t nameCount = 0, nameCap = 0;
      while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected imported name");
          free(names);
          return NULL;
        }
        AstImportName name = {p->cur.start, p->cur.len, NULL, 0};
        advance(p);
        if (match(p, TK_AS)) {
          if (!check(p, TK_ID)) {
            errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected alias after 'as'");
            free(names);
            return NULL;
          }
          name.asName = p->cur.start;
          name.asLen = p->cur.len;
          advance(p);
        }
        MS_ARRAY_PUSH(names, nameCount, nameCap, name);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
      if (!match(p, TK_RBRACE)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}' in from-import");
        free(names);
        return NULL;
      }
      node->as.importStmt.nameCount = nameCount;
      node->as.importStmt.names = astArenaAlloc(p->arena, sizeof(AstImportName) * (size_t)nameCount);
      if (nameCount > 0) {
        memcpy(node->as.importStmt.names, names, sizeof(AstImportName) * (size_t)nameCount);
      }
      free(names);
    }
  } else if (match(p, TK_AS)) {
    if (!check(p, TK_ID)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected alias after 'as'");
      return NULL;
    }
    node->as.importStmt.alias = p->cur.start;
    node->as.importStmt.aliasLen = p->cur.len;
    advance(p);
  }
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}
```

Hook: `if (check(p, TK_IMPORT) || check(p, TK_FROM)) { return parseImportStmt(p); }`.

- [ ] **Step 4: Run tests**
- [ ] **Step 5: Commit**

```bash
git add src/parser/ast.h src/parser/parser.h src/parser/parser.c tests/unit/test_parser_complex.c
git commit -m "✨ feat(parser): parse three import forms with literal path check"
```

### Task 10: class / trait + members + this check

**Files:**
- Modify: `src/parser/ast.h`, `src/parser/parser.h`, `src/parser/parser.c`, `tests/unit/test_parser_complex.c`

- [ ] **Step 1: Write the failing tests**

```c
TEST(classDeclFull) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("class Impl : Base, Mix {\nvar x, y = 2\nfunc m(a) { return a }\nstatic var t = 0\nstatic func s() -> 1\nconst VERSION = \"1.0\"\n}",
                          &arena, &diags);
  AstNode* c = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_CLASS_DECL, c->kind);
  EXPECT_EQ(ctx, false, c->as.classDecl.isTrait);
  EXPECT_STREQ(ctx, "Impl", strOf(c->as.classDecl.name, c->as.classDecl.nameLen));
  EXPECT_EQ(ctx, 2, c->as.classDecl.baseCount);
  EXPECT_STREQ(ctx, "Base", strOf(c->as.classDecl.bases[0]->as.ident.name, c->as.classDecl.bases[0]->as.ident.nameLen));
  EXPECT_STREQ(ctx, "Mix", strOf(c->as.classDecl.bases[1]->as.ident.name, c->as.classDecl.bases[1]->as.ident.nameLen));
  EXPECT_EQ(ctx, 6, c->as.classDecl.memberCount);  // var x + var y + func m + static var t + static func s + const VERSION
  AstNode* m0 = c->as.classDecl.members[0];
  EXPECT_EQ(ctx, AST_VAR_DECL, m0->kind);
  EXPECT_EQ(ctx, NULL, m0->as.varDecl.init);
  AstNode* m1 = c->as.classDecl.members[1];
  EXPECT_EQ(ctx, AST_VAR_DECL, m1->kind);
  EXPECT_EQ(ctx, 2, (int)m1->as.varDecl.init->as.intLit.v);
  AstNode* m2 = c->as.classDecl.members[2];
  EXPECT_EQ(ctx, AST_FUNC_DECL, m2->kind);
  EXPECT_EQ(ctx, 0, m2->as.funcDecl.flags);
  AstNode* m3 = c->as.classDecl.members[3];
  EXPECT_EQ(ctx, AST_VAR_STATIC, m3->as.varDecl.flags);
  AstNode* m4 = c->as.classDecl.members[4];
  EXPECT_EQ(ctx, AST_FUNC_DECL, m4->kind);
  EXPECT_EQ(ctx, AST_FUNC_STATIC, m4->as.funcDecl.flags);
  EXPECT_EQ(ctx, AST_INT, m4->as.funcDecl.exprBody->kind);
  AstNode* m5 = c->as.classDecl.members[5];
  EXPECT_EQ(ctx, AST_VAR_CONST, m5->as.varDecl.flags);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(classDunderInitAndThis) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("class A {\nfunc __init__(n) {\nthis.x = n\n}\n}", &arena, &diags);
  AstNode* c = mod->as.list.items[0];
  AstNode* init = c->as.classDecl.members[0];
  EXPECT_STREQ(ctx, "__init__", strOf(init->as.funcDecl.name, init->as.funcDecl.nameLen));
  AstNode* assign = init->as.funcDecl.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_ASSIGN, assign->kind);
  EXPECT_EQ(ctx, AST_THIS, assign->as.assign.target->as.memberExpr.obj->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(traitDecl) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("trait Logger {\nfunc log(msg) { print(msg) }\n}", &arena, &diags);
  AstNode* t = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_CLASS_DECL, t->kind);
  EXPECT_EQ(ctx, true, t->as.classDecl.isTrait);
  EXPECT_EQ(ctx, 0, t->as.classDecl.baseCount);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errClassThisInFieldInit) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("class A {\nvar t = this.x\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_CLASS_THIS, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errClassBadMember) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("class A {\nx = 1\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(docExamples0506) {
  // 05 §1 全部四种声明 + 06 §1 Animal + 06 §2 Impl 示例解析零诊断
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func add(a, b) { return a + b }\nfunc double(x) -> x * 2\n"
                          "func greet(name, prefix = \"Mr.\") { return name }\nfunc varargs(fmt, *args) { return fmt }",
                          &arena, &diags);
  EXPECT_EQ(ctx, 4, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  diags = (MsDiagSink){0};
  mod = parseSrc("class Animal {\nvar name, age\nfunc __init__(name, age = 1) {\nthis.name = name\nthis.age = age\n}\n"
                 "func speak() -> \"...\"\nfunc describe() -> \"x\"\n}\nvar a = Animal(\"Cat\", 3)\nprint(a.describe())",
                  &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  diags = (MsDiagSink){0};
  mod = parseSrc("class Impl : Base, Logger {\nfunc who() -> \"impl\"\nfunc call() -> \"c\"\n}", &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 2: Run to verify failure** — compile error (`classDecl` payload / `AST_THIS` / `MSE_SYN_CLASS_THIS` missing).

- [ ] **Step 3: Implement**

`src/parser/parser.h`: `MSE_SYN_CLASS_THIS = 1016`.

`src/parser/ast.h`: add `AST_THIS` to `AstKind` (after `AST_IDENT`), `AST_VAR_STATIC = 4` to the var-flags enum, union member (after `importStmt`):

```c
    struct {
      const char* name;
      int32_t nameLen;
      bool isTrait;          // trait 声明（06 §2）
      AstNode** bases;       // AST_IDENT 清单（首基类，余 mixin）
      int32_t baseCount;
      AstNode** members;     // AST_VAR_DECL / AST_FUNC_DECL
      int32_t memberCount;
    } classDecl;
```

`src/parser/parser.c`:

(a) `parsePrimary` new case (after `TK_ID`):

```c
    case TK_THIS: {  // this（06 §3；类体字段初始化合法性见 exprHasThis）
      AstNode* node = astNewNode(p->arena, AST_THIS, line, col);
      advance(p);
      return node;
    }
```

(b) this-walker + static prefix + class parser (after `parseImportStmt`):

```c
// 类体字段初始化 this 检查（06 §3）：表达式树递归（函数字面体属自身作用域，不检查）
static bool exprHasThis(const AstNode* n) {
  if (n == NULL) {
    return false;
  }
  switch (n->kind) {
    case AST_THIS:
      return true;
    case AST_UNARY:
    case AST_RECV:
    case AST_AWAIT:
    case AST_GO:
      return exprHasThis(n->as.unary.operand);
    case AST_BINARY:
    case AST_LOGIC:
      return exprHasThis(n->as.binary.lhs) || exprHasThis(n->as.binary.rhs);
    case AST_TERNARY:
      return exprHasThis(n->as.ternary.cond) || exprHasThis(n->as.ternary.thenExpr) ||
             exprHasThis(n->as.ternary.elseExpr);
    case AST_CALL:
      if (exprHasThis(n->as.call.callee)) {
        return true;
      }
      for (int32_t i = 0; i < n->as.call.count; i++) {
        if (exprHasThis(n->as.call.args[i])) {
          return true;
        }
      }
      return false;
    case AST_INDEX:
      return exprHasThis(n->as.indexExpr.obj) || exprHasThis(n->as.indexExpr.index);
    case AST_MEMBER:
      return exprHasThis(n->as.memberExpr.obj);
    case AST_SLICE:
      return exprHasThis(n->as.slice.obj) || exprHasThis(n->as.slice.start) || exprHasThis(n->as.slice.end) ||
             exprHasThis(n->as.slice.step);
    case AST_LIST:
    case AST_TUPLE:
      for (int32_t i = 0; i < n->as.list.count; i++) {
        if (exprHasThis(n->as.list.items[i])) {
          return true;
        }
      }
      return false;
    case AST_MAP:
      for (int32_t i = 0; i < n->as.mapLit.count; i++) {
        if (exprHasThis(n->as.mapLit.keys[i]) || exprHasThis(n->as.mapLit.values[i])) {
          return true;
        }
      }
      return false;
    case AST_COMPREHENSION:
    case AST_GEN_EXPR:
      if (exprHasThis(n->as.compr.key) || exprHasThis(n->as.compr.value)) {
        return true;
      }
      for (int32_t i = 0; i < n->as.compr.forCount; i++) {
        if (exprHasThis(n->as.compr.fors[i].iter)) {
          return true;
        }
      }
      for (int32_t i = 0; i < n->as.compr.condCount; i++) {
        if (exprHasThis(n->as.compr.conds[i])) {
          return true;
        }
      }
      return false;
    case AST_STRING:
      for (int32_t i = 0; i < n->as.strLit.count; i++) {
        if (exprHasThis(n->as.strLit.segs[i].expr)) {
          return true;
        }
      }
      return false;
    default:  // 字面量/IDENT/FUNC_EXPR 等叶子或独立作用域子树
      return false;
  }
}

// 类体 static 前缀（06 §4）：static 非保留字，后随 func/var 方可识别
static bool isStaticPrefix(Parser* p) {
  return check(p, TK_ID) && p->cur.len == 6 && memcmp(p->cur.start, "static", 6) == 0 &&
         (peekType(p) == TK_FUNC || peekType(p) == TK_VAR);
}

// class/trait 声明（06 §1 §2 §4）：继承清单 + 类体成员
static AstNode* parseClassDecl(Parser* p, bool isTrait) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // class / trait
  if (!check(p, TK_ID)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected class name");
    return NULL;
  }
  const char* name = p->cur.start;
  int32_t nameLen = p->cur.len;
  int32_t nameLine = p->cur.line, nameCol = p->cur.col;
  advance(p);
  if (scopeDeclare(p, name, nameLen)) {  // 类名登记（与 var/类名冲突检出；方法名不查重 05 §1）
    errPos(p, nameLine, nameCol, nameLen, MSE_SYN_DUP_DECL, "duplicate declaration in the same scope");
    return NULL;
  }
  AstNode** bases = NULL;
  int32_t baseCount = 0, baseCap = 0;
  if (match(p, TK_COLON)) {  // 继承清单：首基类，余 mixin（06 §2）
    do {
      if (!check(p, TK_ID)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected base class name");
        goto fail;
      }
      AstNode* base = astNewNode(p->arena, AST_IDENT, p->cur.line, p->cur.col);
      base->as.ident.name = p->cur.start;
      base->as.ident.nameLen = p->cur.len;
      advance(p);
      MS_ARRAY_PUSH(bases, baseCount, baseCap, base);
    } while (match(p, TK_COMMA));
  }
  if (!match(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{' in class body");
    goto fail;
  }
  AstNode** members = NULL;
  int32_t memberCount = 0, memberCap = 0;
  while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
    if (match(p, TK_SEMICOLON)) {
      continue;
    }
    ms_u8 staticFlag = 0;
    if (isStaticPrefix(p)) {
      staticFlag = AST_VAR_STATIC;
      advance(p);
    }
    if (check(p, TK_VAR)) {  // 字段声明：var a, b = 1（每名一节点，06 §1）
      advance(p);
      for (;;) {
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected field name");
          goto fail;
        }
        int32_t fieldLine = p->cur.line, fieldCol = p->cur.col;
        const char* fieldName = p->cur.start;
        int32_t fieldLen = p->cur.len;
        advance(p);
        AstNode* field = astNewNode(p->arena, AST_VAR_DECL, fieldLine, fieldCol);
        field->as.varDecl.name = fieldName;
        field->as.varDecl.nameLen = fieldLen;
        field->as.varDecl.flags = staticFlag;
        if (match(p, TK_EQ)) {
          field->as.varDecl.init = parseExprList(p);
          if (field->as.varDecl.init == NULL) {
            goto fail;
          }
          if (exprHasThis(field->as.varDecl.init)) {  // 06 §3
            errPos(p, fieldLine, fieldCol, 4, MSE_SYN_CLASS_THIS, "'this' is not allowed in field initializer");
            goto fail;
          }
        }
        MS_ARRAY_PUSH(members, memberCount, memberCap, field);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
    } else if (check(p, TK_CONST)) {  // 类常量（06 §4）
      advance(p);
      if (!check(p, TK_ID)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected constant name");
        goto fail;
      }
      int32_t fieldLine = p->cur.line, fieldCol = p->cur.col;
      const char* fieldName = p->cur.start;
      int32_t fieldLen = p->cur.len;
      advance(p);
      AstNode* field = astNewNode(p->arena, AST_VAR_DECL, fieldLine, fieldCol);
      field->as.varDecl.name = fieldName;
      field->as.varDecl.nameLen = fieldLen;
      field->as.varDecl.flags = AST_VAR_CONST | staticFlag;
      if (match(p, TK_EQ)) {
        field->as.varDecl.init = parseExprList(p);
        if (field->as.varDecl.init == NULL) {
          goto fail;
        }
        if (exprHasThis(field->as.varDecl.init)) {
          errPos(p, fieldLine, fieldCol, 5, MSE_SYN_CLASS_THIS, "'this' is not allowed in field initializer");
          goto fail;
        }
      }
      MS_ARRAY_PUSH(members, memberCount, memberCap, field);
    } else if (check(p, TK_FUNC)) {  // 方法（含 dunder，06 §5）
      ms_u8 flags = staticFlag != 0 ? AST_FUNC_STATIC : 0;
      AstNode* method = parseFuncDeclStmt(p, flags, p->cur.line, p->cur.col);
      if (method == NULL) {
        goto fail;
      }
      MS_ARRAY_PUSH(members, memberCount, memberCap, method);
    } else {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'var', 'const' or 'func' in class body");
      goto fail;
    }
  }
  if (!match(p, TK_RBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}' in class body");
    goto fail;
  }
  AstNode* node = astNewNode(p->arena, AST_CLASS_DECL, line, col);
  node->as.classDecl.name = name;
  node->as.classDecl.nameLen = nameLen;
  node->as.classDecl.isTrait = isTrait;
  node->as.classDecl.baseCount = baseCount;
  node->as.classDecl.bases = astNewNodeArray(p->arena, baseCount);
  if (baseCount > 0) {
    memcpy(node->as.classDecl.bases, bases, sizeof(AstNode*) * (size_t)baseCount);
  }
  node->as.classDecl.memberCount = memberCount;
  node->as.classDecl.members = astNewNodeArray(p->arena, memberCount);
  if (memberCount > 0) {
    memcpy(node->as.classDecl.members, members, sizeof(AstNode*) * (size_t)memberCount);
  }
  free(bases);
  free(members);
  return node;
fail:
  free(bases);
  free(members);
  return NULL;
}
```

Hooks in `parseStatement`:

```c
  if (check(p, TK_CLASS)) {
    return parseClassDecl(p, false);
  }
  if (check(p, TK_TRAIT)) {
    return parseClassDecl(p, true);
  }
```

- [ ] **Step 4: Run tests**
- [ ] **Step 5: Commit**

```bash
git add src/parser/ast.h src/parser/parser.h src/parser/parser.c tests/unit/test_parser_complex.c
git commit -m "✨ feat(parser): parse class/trait declarations with members and this check"
```

### Task 11: Full verification, doc examples, and task status

**Files:**
- Modify: `tests/unit/test_parser_complex.c`, `docs/tasks/T008-complex-statements.md`, `docs/tasks/README.md`

- [ ] **Step 1: Add doc-example smoke tests (04 §4/§5.2 full blocks)**

```c
TEST(docExample04ForAndSwitch) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func demo(xs, m, x, v) {\nfor i := 0; i < 10; i++ { f(i) }\nfor x2 in xs { f(x2) }\n"
                          "for k, v2 in m.items() { f(k) }\nfor x2 in range(10) { f(x2) }\nfor x > 0 { f(x) }\nfor { f(1) }\nwhile x < 3 { f(x) }\n"
                          "switch x {\ncase 1, 2: f(x)\ncase 3: f(x)\ndefault: f(x)\n}\n"
                          "switch type(v) {\ncase int, bigint: f(v)\ncase string as s: f(len(s))\ncase Point as p: p.move(1, 1)\ndefault: f(v)\n}\n}",
                          &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 10, mod->as.list.items[0]->as.funcDecl.body->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}
```

Register every test from Tasks 1-11 in `main` in declaration order (same pattern as `test_parser_expr.c`):

```c
int main(void) {
  TestCtx ctx = {0};
  recvExprOnAssignRhs(&ctx);
  recvDiscardStmt(&ctx);
  awaitAndGoExprs(&ctx);
  sendStmt(&ctx);
  unaryBindsLooserThanRecv(&ctx);
  funcDeclFullForms(&ctx);
  funcDeclGeneratorAndAsync(&ctx);
  funcDeclDefaultString(&ctx);
  errParamOrder(&ctx);
  funcRedefinitionAllowed(&ctx);
  funcExprDefaultsRestExprBody(&ctx);
  forTriWithSugarPost(&ctx);
  forTriEmptySegments(&ctx);
  forTriPartialSegments(&ctx);
  forInForms(&ctx);
  forCondAndInfinite(&ctx);
  whileForm(&ctx);
  errForBadTarget(&ctx);
  forHeaderNoInBodyRestoredIn(&ctx);
  throwForms(&ctx);
  switchValueForm(&ctx);
  switchTypeFormWithAs(&ctx);
  switchSubjectNamedTypeStaysValue(&ctx);
  errSwitchMissingColon(&ctx);
  tryCatchFinallyForms(&ctx);
  errCatchOrder(&ctx);
  errTryWithoutCatchFinally(&ctx);
  deferRegistersCall(&ctx);
  errDeferAtModuleLevel(&ctx);
  errDeferNotCall(&ctx);
  selectThreeBranchForms(&ctx);
  importForms(&ctx);
  errImportPathNotLiteral(&ctx);
  classDeclFull(&ctx);
  classDunderInitAndThis(&ctx);
  traitDecl(&ctx);
  errClassThisInFieldInit(&ctx);
  errClassBadMember(&ctx);
  docExamples0506(&ctx);
  docExample04ForAndSwitch(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
```

- [ ] **Step 2: Full build + full suite**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
Expected: 100% pass (all unit + fixture + conformance).

- [ ] **Step 3: clang-format touched files**

Run: `clang-format -i src/parser/parser.c src/parser/ast.h src/parser/parser.h tests/unit/test_parser_complex.c` then rebuild + retest.
(If clang-format is unavailable on PATH, note it and skip — CI will catch.)

- [ ] **Step 4: Update task docs**

`docs/tasks/T008-complex-statements.md`: header 状态 → 已完成; tick all DoD checkboxes; add 备注:
- 单 ID catch 子句按裸绑定解析（`catch ValueError {}` 的类型复核归 T023 binder）
- `i++`/`i--` 仅 for 第三段语法糖（04 §4 示例），语言无通用自增运算符
- `static` 非保留字（01 §2），类体按 ID "static"+peek(func/var) 识别

`docs/tasks/README.md`: T008 row 状态 ⬜ → ✅.

- [ ] **Step 5: Final commits**

```bash
git add tests/unit/test_parser_complex.c
git commit -m "✅ test(parser): cover 04/05/06 doc examples for complex statements"
git add docs/tasks/T008-complex-statements.md docs/tasks/README.md
git commit -m "📝 docs(tasks): mark T008 done"
```

(If clang-format changed sources, first: `git add -u src tests && git commit -m "🎨 style(parser): clang-format T008 sources"`.)

---

## Self-Review (done at plan time)

- **Spec coverage:** task §2 bullets → Tasks 1 (send/recv/await/go), 2 (func decl 全形态 + yield), 3 (for 五形态 + while + i++ 糖 + break/continue), 4 (throw), 5 (switch 值/类型 + as), 6 (try/catch/finally + 排序), 7 (defer + 模块层报错), 8 (select), 9 (import 三式 + 路径校验), 10 (class/trait + static/var/const/func + this). DoD 六项均有对应测试（04/05/06 示例 = Task 10/11 docExamples*；五形态判别 = Task 3；select AST = Task 8；catch 排序 = Task 6；函数全排列 = Task 2；class = Task 10）。ms 用例（§5.2）明确归 T009 快照 conformance，不在本任务。
- **Known deviations (documented):** catch 单 ID 歧义按裸绑定（见 Context）；`AST_THROW/DEFER/YIELD` 复用 exprStmt、`AST_RECV/AWAIT/GO` 复用 unary（跟随 T007 载荷复用惯例）。
- **Type consistency:** `finishSimpleStmt(p, first, firstStar, line, col)` used by both `parseSimpleStmt` and `parseForPost`; `parseFuncDeclStmt(p, flags, line, col)` used by statement dispatch and class body; `peekType` shared by switch/static/for-post; `strOf` helper shared by tests.

