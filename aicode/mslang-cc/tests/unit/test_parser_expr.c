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
