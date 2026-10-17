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

// 借用源文本的非 NUL 结尾名 → 静态缓冲（单断言内使用，勿跨用例持有）
static const char* strOf(const char* start, int32_t len) {
  static char buf[64];
  memcpy(buf, start, (size_t)(len < 63 ? len : 63));
  buf[len < 63 ? len : 63] = '\0';
  return buf;
}

TEST(recvExprOnAssignRhs) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("v = <-ch", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  AstNode* assign = mod->as.list.items[0];  // 赋值语句节点即 ASSIGN 本身（无 exprStmt 包裹）
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

TEST(stmtFuncExprFormsAndIife) {
  // 语句位函数表达式三形态 + 裸 IIFE 走后缀调用（T007 能力保持）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func(x) -> x\nfunc*(y) { yield y }\nfunc() { }()", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, AST_FUNC_EXPR, mod->as.list.items[0]->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, AST_FUNC_GENERATOR, mod->as.list.items[1]->as.exprStmt.expr->as.funcExpr.flags);
  AstNode* call = mod->as.list.items[2]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_CALL, call->kind);
  EXPECT_EQ(ctx, AST_FUNC_EXPR, call->as.call.callee->kind);
  // rest 参数带默认值 → 参数序错误
  diags = (MsDiagSink){0};
  parseSrc("func f(*r = 1) { }", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_PARAM_ORDER, diags.items[0].code);
  EXPECT_EQ(ctx, 9, diags.items[0].col);  // 诊断定位在参数名 r（col 9）而非其后的 =
  astArenaDestroy(&arena);
}

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

TEST(breakContinueForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f(xs) {\nfor x in xs {\ncontinue\n}\n}", &arena, &diags);
  AstNode* body = mod->as.list.items[0]->as.funcDecl.body;
  AstNode* f = body->as.list.items[0];
  EXPECT_EQ(ctx, AST_CONTINUE, f->as.forStmt.body->as.list.items[0]->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(throwForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("throw ValueError(\"bad input: ${x}\")", &arena, &diags);
  AstNode* t = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_THROW, t->kind);
  EXPECT_EQ(ctx, AST_CALL, t->as.exprStmt.expr->kind);
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

TEST(forInBadTargetRecoversInPlace) {
  // 就地恢复：init 非 EXPR_STMT 时 target 为 NULL，仅 1 条诊断（不级联 unmatched '}'）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f() { for i := 0 in xs { } }", &arena, &diags);
  AstNode* fo = mod->as.list.items[0]->as.funcDecl.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_FOR, fo->kind);
  EXPECT_EQ(ctx, AST_FOR_IN, fo->as.forStmt.form);
  EXPECT_EQ(ctx, NULL, fo->as.forStmt.target);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_BAD_TARGET, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(forPostDecSugarAndPlainForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod =
      parseSrc("func f() {\nfor i := 9; i > 0; i-- { s() }\nfor j := 0; j < 2; j += 2 { s() }\n}", &arena, &diags);
  AstNode* body = mod->as.list.items[0]->as.funcDecl.body;
  AstNode* a = body->as.list.items[0];
  EXPECT_EQ(ctx, AST_OP_SUB, a->as.forStmt.post->as.assign.op);
  EXPECT_EQ(ctx, 1, (int)a->as.forStmt.post->as.assign.value->as.intLit.v);
  AstNode* b = body->as.list.items[1];
  EXPECT_EQ(ctx, AST_ASSIGN, b->as.forStmt.post->kind);
  EXPECT_EQ(ctx, AST_OP_ADD, b->as.forStmt.post->as.assign.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(forCondSegmentInIsOperator) {
  // 三段式 cond 段内 in 仍是运算符（noIn 仅作用于首段）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("for ; a in b; { }", &arena, &diags);
  AstNode* f = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_FOR_TRI, f->as.forStmt.form);
  EXPECT_EQ(ctx, AST_OP_IN, f->as.forStmt.cond->as.binary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(forTriSecondSemicolonLenient) {
  // cond 后第二 ';' 可随 '{' 省略（init-only 循环）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f() { for i := 0; i < 3 { s() } }", &arena, &diags);
  AstNode* fo = mod->as.list.items[0]->as.funcDecl.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_FOR_TRI, fo->as.forStmt.form);
  EXPECT_EQ(ctx, AST_BINARY, fo->as.forStmt.cond->kind);
  EXPECT_EQ(ctx, NULL, fo->as.forStmt.post);
  EXPECT_EQ(ctx, 0, diags.count);
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

TEST(forHeaderNoInNotLeakingIntoFuncBody) {
  // for 首段内嵌函数字面量体内 in 恢复为运算符（noIn 仅作用于首段自身）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func t() { for g = func() { return a in b }; c ; p { } }", &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* fo = mod->as.list.items[0]->as.funcDecl.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_FOR_TRI, fo->as.forStmt.form);
  AstNode* fn = fo->as.forStmt.init->as.assign.value;
  EXPECT_EQ(ctx, AST_FUNC_EXPR, fn->kind);
  AstNode* ret = fn->as.funcExpr.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_RETURN, ret->kind);
  EXPECT_EQ(ctx, AST_BINARY, ret->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, AST_OP_IN, ret->as.exprStmt.expr->as.binary.op);
  astArenaDestroy(&arena);
}

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

TEST(switchCaseBodyCanBeBlock) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("switch x {\ncase 1: {\nf()\ng()\n}\n}", &arena, &diags);
  AstNode* sw = mod->as.list.items[0];
  AstNode* c0 = sw->as.switchStmt.cases[0];
  EXPECT_EQ(ctx, AST_BLOCK, c0->as.switchCase.body->kind);
  EXPECT_EQ(ctx, 2, c0->as.switchCase.body->as.list.count);
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

TEST(switchRecoveryKeepsFollowingStmt) {
  // 块内坏 switch 就地恢复：后续语句存活、函数 } 不被窃取、恰 1 条诊断
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f() {\nswitch x { case 1 f() }\nvar g = 2\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  AstNode* body = mod->as.list.items[0]->as.funcDecl.body;
  EXPECT_EQ(ctx, 1, body->as.list.count);
  EXPECT_EQ(ctx, AST_VAR_DECL, body->as.list.items[0]->kind);
  EXPECT_STREQ(ctx, "g", strOf(body->as.list.items[0]->as.varDecl.name, body->as.list.items[0]->as.varDecl.nameLen));
  astArenaDestroy(&arena);
  // 模块级同构探针：恢复后 var 声明存活、恰 1 条诊断
  MsDiagSink modDiags = {0};
  AstArena modArena;
  astArenaInit(&modArena);
  AstNode* mod2 = parseSrc("switch x { case 1 f() }\nvar g = 2", &modArena, &modDiags);
  EXPECT_EQ(ctx, 1, mod2->as.list.count);
  EXPECT_EQ(ctx, 1, modDiags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, modDiags.items[0].code);
  EXPECT_EQ(ctx, AST_VAR_DECL, mod2->as.list.items[0]->kind);
  astArenaDestroy(&modArena);
}

TEST(switchErrorPaths) {
  // 四类结构性错误：首条诊断均为 MSE_SYN_EXPECTED
  const char* srcs[] = {
      "switch type(v { case 1: f() }",           // type( 缺 )
      "switch x case 1: f()",                    // 缺 {（单行：换行版经 ASI 级联次级诊断）
      "switch type(v) { case 1: f() }",          // 类型式 case 值非类型名
      "switch type(v) { case string as : f() }"  // as 后缺 ident
  };
  for (int32_t i = 0; i < 4; i++) {
    MsDiagSink diags = {0};
    AstArena arena;
    astArenaInit(&arena);
    parseSrc(srcs[i], &arena, &diags);
    EXPECT_EQ(ctx, true, diags.count >= 1);
    if (diags.count >= 1) {
      EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
    }
    astArenaDestroy(&arena);
  }
}

TEST(tryCatchFinallyForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod =
      parseSrc("try {\nrisky(x)\n} catch A, B {\ng()\n} catch e {\nprint(e)\n} finally {\nh()\n}", &arena, &diags);
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
  AstNode* mod = parseSrc("try{}catch e{}catch V{}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_CATCH_ORDER, diags.items[0].code);
  EXPECT_EQ(ctx, 1, diags.items[0].line);
  EXPECT_EQ(ctx, AST_TRY, mod->as.list.items[0]->kind);
  astArenaDestroy(&arena);
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("try{}catch e{}catch A, B{}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
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

TEST(bareThrowRethrowInCatch) {
  // 裸 throw 在 catch 块内为重抛（07 §3）：value 为 NULL
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f() { try { g() } catch e { throw } }", &arena, &diags);
  AstNode* tryStmt = mod->as.list.items[0]->as.funcDecl.body->as.list.items[0];
  AstNode* bare = tryStmt->as.tryStmt.catches[0].block->as.list.items[0];
  EXPECT_EQ(ctx, AST_THROW, bare->kind);
  EXPECT_EQ(ctx, NULL, bare->as.exprStmt.expr);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(catchRecoveryEdges) {
  // 类型清单尾逗号缺类型名：就地恢复恰 1 条诊断
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("try{}catch A, { }", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  AstNode* t = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_TRY, t->kind);
  EXPECT_EQ(ctx, 1, t->as.tryStmt.catchCount);
  EXPECT_EQ(ctx, 1, t->as.tryStmt.catches[0].typeCount);  // 已收集的 A 照常提交
  astArenaDestroy(&arena);
  // 裸绑定后多条子句：仅首条违序报错，子句照常并入
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  mod = parseSrc("try{}catch e{}catch V{}catch W{}", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_CATCH_ORDER, diags.items[0].code);
  t = mod->as.list.items[0];
  EXPECT_EQ(ctx, 3, t->as.tryStmt.catchCount);
  astArenaDestroy(&arena);
  // 连续逗号：就地恢复后子句块照常完成，恰 1 条诊断
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  mod = parseSrc("try{}catch A,,B{ }", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  t = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_TRY, t->kind);
  EXPECT_EQ(ctx, 1, t->as.tryStmt.catchCount);
  astArenaDestroy(&arena);
}

TEST(deferRegistersCall) {
  // defer 注册调用（04 §7）：载荷复用 exprStmt.expr，callee 为成员访问
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod =
      parseSrc("func copyFile(src, dst) {\nvar f = open(src)\ndefer f.close()\nreturn open(dst)\n}", &arena, &diags);
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
  // defer 仅函数体内合法（04 §7）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("defer f.close()", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DEFER_TOPLEVEL, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errDeferNotCall) {
  // defer 操作数必须是调用表达式（04 §7）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("func f() {\ndefer x\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DEFER_CALL, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(deferNestedFuncAndExprBody) {
  // funcDepth 只在函数体内 >0：嵌套函数内 defer 合法；表达式体不可能出现 defer（语法排除）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var g = func() -> f()", &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, AST_FUNC_EXPR, mod->as.list.items[0]->as.varDecl.init->kind);
  astArenaDestroy(&arena);
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("func outer() {\nvar inner = func() {\ndefer cleanup()\n}\n}", &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(selectThreeBranchForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod =
      parseSrc("select {\ncase v = <-ch1: print(v)\ncase ch2 <- x: send()\ncase <-tick: tock()\ndefault: none()\n}",
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

TEST(selectCaseBodyBlockAndErrors) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("select {\ncase <-ch: {\nf()\ng()\n}\n}", &arena, &diags);
  AstNode* sel = mod->as.list.items[0];
  AstNode* c0 = sel->as.selectStmt.cases[0];
  EXPECT_EQ(ctx, AST_BLOCK, c0->as.selectCase.body->kind);
  EXPECT_EQ(ctx, 2, c0->as.selectCase.body->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
  // 非法 case 形态（无 = / <-）：恰 1 条诊断
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("select {\ncase f(): g()\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  astArenaDestroy(&arena);
  // 接收赋值目标非法
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("select {\ncase 1 = <-ch: g()\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_BAD_TARGET, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(selectRecoveryKeepsFollowingStmt) {
  // 坏 select 就地恢复：后续 x = <-ch 存活、函数 } 不被窃取、恰 1 条诊断（T006 §2 先例）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func f() {\nselect { case f(): g() }\nx = <-ch\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  AstNode* body = mod->as.list.items[0]->as.funcDecl.body;
  EXPECT_EQ(ctx, 1, body->as.list.count);
  EXPECT_EQ(ctx, AST_ASSIGN, body->as.list.items[0]->kind);
  EXPECT_EQ(ctx, AST_RECV, body->as.list.items[0]->as.assign.value->kind);
  astArenaDestroy(&arena);
  // 正常 select 后续语句无扰动（恢复边界外的正常路径）
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  AstNode* mod2 = parseSrc("func g() {\nselect { case v = <-ch1: f() }\nvar t = 2\n}", &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* body2 = mod2->as.list.items[0]->as.funcDecl.body;
  EXPECT_EQ(ctx, 2, body2->as.list.count);
  EXPECT_EQ(ctx, AST_SELECT, body2->as.list.items[0]->kind);
  EXPECT_EQ(ctx, AST_VAR_DECL, body2->as.list.items[1]->kind);
  astArenaDestroy(&arena);
}

TEST(selectSendMemberChanAndIdentUse) {
  // send 分支目标可为成员/索引（parseExpr 兜底，无赋值目标检查）；default 可独占
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("select {\ncase q.a <- 1: f()\ncase xs[0] <- 5: g()\ndefault: h()\n}", &arena, &diags);
  AstNode* sel = mod->as.list.items[0];
  EXPECT_EQ(ctx, 3, sel->as.selectStmt.count);
  EXPECT_EQ(ctx, AST_MEMBER, sel->as.selectStmt.cases[0]->as.selectCase.chan->kind);
  EXPECT_EQ(ctx, AST_INDEX, sel->as.selectStmt.cases[1]->as.selectCase.chan->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
  // select 非 { 前置时仍是普通标识符（01 §2 非保留字）
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  AstNode* mod2 = parseSrc("var select = 1\nselect = 2", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod2->as.list.count);
  EXPECT_EQ(ctx, AST_VAR_DECL, mod2->as.list.items[0]->kind);
  EXPECT_EQ(ctx, AST_ASSIGN, mod2->as.list.items[1]->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(importForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc(
      "import \"strutil\"\nimport \"net/addr\" as netaddr\nfrom \"strutil\" import { join, split as sp, pad }\nfrom "
      "\"strutil\" import *",
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
  // 空具名清单 from "m" import { }：允许，nameCount 0（无害）
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  AstNode* e = parseSrc("from \"m\" import { }", &arena, &diags);
  EXPECT_EQ(ctx, 1, e->as.list.count);
  EXPECT_EQ(ctx, AST_IMPORT, e->as.list.items[0]->kind);
  EXPECT_EQ(ctx, 0, e->as.list.items[0]->as.importStmt.nameCount);
  EXPECT_EQ(ctx, false, e->as.list.items[0]->as.importStmt.star);
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
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("import \"a${b}c\"", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_IMPORT_PATH, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(importRawStringPathAndInFunc) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("import `raw/mod`\nfunc f() {\nfrom \"m\" import { a }\n}", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_STREQ(ctx, "raw/mod",
               strOf(mod->as.list.items[0]->as.importStmt.path, mod->as.list.items[0]->as.importStmt.pathLen));
  AstNode* body = mod->as.list.items[1]->as.funcDecl.body;
  EXPECT_EQ(ctx, AST_IMPORT, body->as.list.items[0]->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(classDeclFull) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc(
      "class Impl : Base, Mix {\nvar x, y = 2\nfunc m(a) { return a }\nstatic var t = 0\nstatic func s() -> 1\nconst "
      "VERSION = \"1.0\"\n}",
      &arena, &diags);
  AstNode* c = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_CLASS_DECL, c->kind);
  EXPECT_EQ(ctx, false, c->as.classDecl.isTrait);
  EXPECT_STREQ(ctx, "Impl", strOf(c->as.classDecl.name, c->as.classDecl.nameLen));
  EXPECT_EQ(ctx, 2, c->as.classDecl.baseCount);
  EXPECT_STREQ(ctx, "Base", strOf(c->as.classDecl.bases[0]->as.ident.name, c->as.classDecl.bases[0]->as.ident.nameLen));
  EXPECT_STREQ(ctx, "Mix", strOf(c->as.classDecl.bases[1]->as.ident.name, c->as.classDecl.bases[1]->as.ident.nameLen));
  EXPECT_EQ(ctx, 6,
            c->as.classDecl.memberCount);  // var x + var y + func m + static var t + static func s + const VERSION
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
  // 深层 this（调用实参内）同样拒绝
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("class B {\nvar u = f(this)\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_CLASS_THIS, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errClassBadMemberAndDupClass) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  parseSrc("class A {\nx = 1\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  astArenaDestroy(&arena);
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("class A {}\nvar A = 1", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DUP_DECL, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(staticIdentNotConfused) {
  // static 非保留字：字段名/方法名可为 static（仅 static+peek(func/var) 才是修饰符）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("class C {\nvar static = 1\nfunc static() { }\n}", &arena, &diags);
  AstNode* c = mod->as.list.items[0];
  EXPECT_EQ(ctx, 2, c->as.classDecl.memberCount);
  EXPECT_EQ(ctx, 0, c->as.classDecl.members[0]->as.varDecl.flags);
  EXPECT_EQ(ctx, 0, c->as.classDecl.members[1]->as.funcDecl.flags);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(classFieldInitCommaAndStaticConst) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  // 逗号分隔字段：初始化不吸收逗号
  AstNode* mod = parseSrc("class A {\nvar a = 1, b\n}", &arena, &diags);
  AstNode* c = mod->as.list.items[0];
  EXPECT_EQ(ctx, 2, c->as.classDecl.memberCount);
  EXPECT_EQ(ctx, 1, (int)c->as.classDecl.members[0]->as.varDecl.init->as.intLit.v);
  EXPECT_EQ(ctx, NULL, c->as.classDecl.members[1]->as.varDecl.init);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
  // 第二字段 this 初始化 → 1016 定位于该字段
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("class B {\nvar a = 1, b = this.a\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_CLASS_THIS, diags.items[0].code);
  astArenaDestroy(&arena);
  // static const 组合标志
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  mod = parseSrc("class C {\nstatic const V = \"1\"\n}", &arena, &diags);
  AstNode* m = mod->as.list.items[0]->as.classDecl.members[0];
  EXPECT_EQ(ctx, AST_VAR_CONST | AST_VAR_STATIC, m->as.varDecl.flags);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
  // 类体无名 func 表达式 → 恰 1 条诊断
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("class D {\nfunc (x) { }\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(docExamples0506) {
  // 05 §1 四种声明 + 06 §1 Animal + 06 §2 Impl 示例解析零诊断
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc(
      "func add(a, b) { return a + b }\nfunc double(x) -> x * 2\n"
      "func greet(name, prefix = \"Mr.\") { return name }\nfunc varargs(fmt, *args) { return fmt }",
      &arena, &diags);
  EXPECT_EQ(ctx, 4, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  diags = (MsDiagSink){0};
  mod = parseSrc(
      "class Animal {\nvar name, age\nfunc __init__(name, age = 1) {\nthis.name = name\nthis.age = age\n}\n"
      "func speak() -> \"...\"\nfunc describe() -> \"x\"\n}\nvar a = Animal(\"Cat\", 3)\nprint(a.describe())",
      &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  diags = (MsDiagSink){0};
  mod = parseSrc("class Impl : Base, Logger {\nfunc who() -> \"impl\"\nfunc call() -> \"c\"\n}", &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(docExample04ForAndSwitch) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc(
      "func demo(xs, m, x, v) {\nfor i := 0; i < 10; i++ { f(i) }\nfor x2 in xs { f(x2) }\n"
      "for k, v2 in m.items() { f(k) }\nfor x2 in range(10) { f(x2) }\nfor x > 0 { f(x) }\nfor { f(1) }\nwhile x < 3 { "
      "f(x) }\n"
      "switch x {\ncase 1, 2: f(x)\ncase 3: f(x)\ndefault: f(x)\n}\n"
      "switch type(v) {\ncase int, bigint: f(v)\ncase string as s: f(len(s))\ncase Point as p: p.move(1, 1)\ndefault: "
      "f(v)\n}\n}",
      &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  // 6 for（三段/迭代/解构/range/条件/无限）+ 1 while + 2 switch = 9 条语句
  EXPECT_EQ(ctx, 9, mod->as.list.items[0]->as.funcDecl.body->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(forHeaderDeclNoEnclosingDup) {
  // 兄弟循环同名头变量 + var 遮蔽：均合法（每轮新鲜绑定 04 §4，作用域归 T009）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc(
      "func f() {\nfor i := 0; i < 3; i++ { a() }\nfor i := 0; i < 3; i++ { b() }\nvar i = 0\nfor i := 5; i < 9; i++ { "
      "c() }\n}",
      &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* body = mod->as.list.items[0]->as.funcDecl.body;
  EXPECT_EQ(ctx, 4, body->as.list.count);
  astArenaDestroy(&arena);
  // 模块层 for 头 := 仍报 1002（04 §1）；首条即 SHORT_TOPLEVEL。
  // 注：单行三段头失败后 synchronize 止于 ';'，残余 i++/{ 触发既有 2 条 1000 级联（修复前即如此，恢复策略另行处理）
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("for i := 0; i < 3; i++ { }", &arena, &diags);
  EXPECT_EQ(ctx, true, diags.count >= 1);
  EXPECT_EQ(ctx, MSE_SYN_SHORT_TOPLEVEL, diags.items[0].code);
  astArenaDestroy(&arena);
  // 块内普通 := 查重仍然生效（非 for 头路径不回归）
  diags = (MsDiagSink){0};
  astArenaInit(&arena);
  parseSrc("func g() {\nx := 1\nx := 2\n}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DUP_DECL, diags.items[0].code);
  astArenaDestroy(&arena);
}

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
  stmtFuncExprFormsAndIife(&ctx);
  forTriWithSugarPost(&ctx);
  forTriEmptySegments(&ctx);
  forTriPartialSegments(&ctx);
  forInForms(&ctx);
  forCondAndInfinite(&ctx);
  whileForm(&ctx);
  breakContinueForms(&ctx);
  throwForms(&ctx);
  errForBadTarget(&ctx);
  forInBadTargetRecoversInPlace(&ctx);
  forPostDecSugarAndPlainForms(&ctx);
  forCondSegmentInIsOperator(&ctx);
  forTriSecondSemicolonLenient(&ctx);
  forHeaderNoInBodyRestoredIn(&ctx);
  forHeaderNoInNotLeakingIntoFuncBody(&ctx);
  switchValueForm(&ctx);
  switchTypeFormWithAs(&ctx);
  switchSubjectNamedTypeStaysValue(&ctx);
  switchCaseBodyCanBeBlock(&ctx);
  errSwitchMissingColon(&ctx);
  switchRecoveryKeepsFollowingStmt(&ctx);
  switchErrorPaths(&ctx);
  tryCatchFinallyForms(&ctx);
  errCatchOrder(&ctx);
  errTryWithoutCatchFinally(&ctx);
  bareThrowRethrowInCatch(&ctx);
  catchRecoveryEdges(&ctx);
  deferRegistersCall(&ctx);
  errDeferAtModuleLevel(&ctx);
  errDeferNotCall(&ctx);
  deferNestedFuncAndExprBody(&ctx);
  selectThreeBranchForms(&ctx);
  selectCaseBodyBlockAndErrors(&ctx);
  selectRecoveryKeepsFollowingStmt(&ctx);
  selectSendMemberChanAndIdentUse(&ctx);
  importForms(&ctx);
  errImportPathNotLiteral(&ctx);
  importRawStringPathAndInFunc(&ctx);
  classDeclFull(&ctx);
  classDunderInitAndThis(&ctx);
  traitDecl(&ctx);
  errClassThisInFieldInit(&ctx);
  errClassBadMemberAndDupClass(&ctx);
  staticIdentNotConfused(&ctx);
  classFieldInitCommaAndStaticConst(&ctx);
  docExamples0506(&ctx);
  docExample04ForAndSwitch(&ctx);
  forHeaderDeclNoEnclosingDup(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
