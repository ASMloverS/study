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
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
