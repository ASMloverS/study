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
  EXPECT_EQ(ctx, 0, diags.count);
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
  EXPECT_EQ(ctx, 9, inner->as.ternary.cond->col);  // 'c' 位于 1-based 列 9
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(ternaryMissingColon) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a ? b c", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errChainedCompareWithIsIn) {
  // a == b is c → 1007（is/in 加入比较集后的链式拦截）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a == b is c", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1007, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(unmappedBinaryBranches) {
  // /、%、>> 分支正例：8 / 4 % 3 → MOD(DIV(8,4),3)；a >> 1 → SHR(a,1)
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("8 / 4 % 3\na >> 1", &arena, &diags);
  AstNode* modexpr = expr0(mod);
  EXPECT_EQ(ctx, AST_OP_MOD, modexpr->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_DIV, modexpr->as.binary.lhs->as.binary.op);
  AstNode* shr = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_OP_SHR, shr->as.binary.op);
  EXPECT_EQ(ctx, 1, (int)shr->as.binary.rhs->as.intLit.v);
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
  logicSpellings(&ctx);
  logicSymbolForms(&ctx);
  logicBindsLooserThanCompare(&ctx);
  ternaryRightAssociative(&ctx);
  ternaryMissingColon(&ctx);
  errChainedCompareWithIsIn(&ctx);
  unmappedBinaryBranches(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
