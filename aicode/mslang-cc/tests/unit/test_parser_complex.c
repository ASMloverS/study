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

int main(void) {
  TestCtx ctx = {0};
  recvExprOnAssignRhs(&ctx);
  recvDiscardStmt(&ctx);
  awaitAndGoExprs(&ctx);
  sendStmt(&ctx);
  unaryBindsLooserThanRecv(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
