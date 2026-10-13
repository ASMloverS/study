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
  AstNode* orNode = expr0(mod);
  EXPECT_EQ(ctx, AST_OP_BIT_OR, orNode->as.binary.op);
  AstNode* xorNode = orNode->as.binary.rhs;
  EXPECT_EQ(ctx, AST_OP_BIT_XOR, xorNode->as.binary.op);
  AstNode* andNode = xorNode->as.binary.rhs;
  EXPECT_EQ(ctx, AST_OP_BIT_AND, andNode->as.binary.op);
  AstNode* shl = andNode->as.binary.rhs;
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
  AstNode* orNode = expr0(mod);
  EXPECT_EQ(ctx, AST_LOGIC, orNode->kind);
  EXPECT_EQ(ctx, AST_OP_OR, orNode->as.binary.op);
  AstNode* andNode = orNode->as.binary.lhs;
  EXPECT_EQ(ctx, AST_LOGIC, andNode->kind);
  EXPECT_EQ(ctx, AST_OP_AND, andNode->as.binary.op);
  AstNode* notNode = orNode->as.binary.rhs;
  EXPECT_EQ(ctx, AST_UNARY, notNode->kind);
  EXPECT_EQ(ctx, AST_UNOP_NOT, notNode->as.unary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(logicSymbolForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a && b || c && d", &arena, &diags);
  AstNode* orNode = expr0(mod);
  EXPECT_EQ(ctx, AST_OP_OR, orNode->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_AND, orNode->as.binary.lhs->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_AND, orNode->as.binary.rhs->as.binary.op);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(logicBindsLooserThanCompare) {
  // x == 1 && y < 2 → AND(EQ(x,1), LT(y,2))
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x == 1 && y < 2", &arena, &diags);
  AstNode* andNode = expr0(mod);
  EXPECT_EQ(ctx, AST_LOGIC, andNode->kind);
  EXPECT_EQ(ctx, AST_OP_EQ, andNode->as.binary.lhs->as.binary.op);
  EXPECT_EQ(ctx, AST_OP_LT, andNode->as.binary.rhs->as.binary.op);
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
  EXPECT_EQ(ctx, 1, (int)call->as.call.args[0]->as.intLit.v);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errSpreadNotLast) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(1, *xs, 2)", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1010, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(sliceForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("xs[a:b:c]\nxs[a:]\nxs[:b]\nxs[::c]\nxs[a:b]\nxs[::]\nxs[:]", &arena, &diags);
  EXPECT_EQ(ctx, 7, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* s0 = expr0(mod);
  EXPECT_EQ(ctx, AST_SLICE, s0->kind);
  EXPECT(ctx, s0->as.slice.start != NULL && s0->as.slice.end != NULL && s0->as.slice.step != NULL);
  EXPECT(ctx, s0->as.slice.start->as.ident.nameLen == 1 && s0->as.slice.start->as.ident.name[0] == 'a');
  EXPECT(ctx, s0->as.slice.end->as.ident.nameLen == 1 && s0->as.slice.end->as.ident.name[0] == 'b');
  EXPECT(ctx, s0->as.slice.step->as.ident.nameLen == 1 && s0->as.slice.step->as.ident.name[0] == 'c');
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
  AstNode* s6 = mod->as.list.items[6]->as.exprStmt.expr;
  EXPECT(ctx, s6->as.slice.start == NULL && s6->as.slice.end == NULL && s6->as.slice.step == NULL);
  astArenaDestroy(&arena);
}

TEST(errCallMissingRparen) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(1", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(ternaryThenNested) {
  // a ? b ? c : d : e → then 分支亦按完整三元解析（右结合家族）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a ? b ? c : d : e", &arena, &diags);
  AstNode* outer = expr0(mod);
  EXPECT_EQ(ctx, AST_TERNARY, outer->kind);
  EXPECT_EQ(ctx, AST_IDENT, outer->as.ternary.cond->kind);
  AstNode* inner = outer->as.ternary.thenExpr;
  EXPECT_EQ(ctx, AST_TERNARY, inner->kind);
  EXPECT_EQ(ctx, AST_IDENT, inner->as.ternary.cond->kind);
  EXPECT_EQ(ctx, AST_IDENT, inner->as.ternary.thenExpr->kind);
  EXPECT_EQ(ctx, AST_IDENT, inner->as.ternary.elseExpr->kind);
  EXPECT_EQ(ctx, AST_IDENT, outer->as.ternary.elseExpr->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(commaLooserThanTernary) {
  // a, b ? c : d → 元组第二元素为三元（逗号松于三元）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a, b ? c : d", &arena, &diags);
  AstNode* tuple = expr0(mod);
  EXPECT_EQ(ctx, AST_TUPLE, tuple->kind);
  EXPECT_EQ(ctx, 2, tuple->as.tuple.count);
  EXPECT_EQ(ctx, AST_IDENT, tuple->as.tuple.items[0]->kind);
  EXPECT_EQ(ctx, AST_TERNARY, tuple->as.tuple.items[1]->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

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
  EXPECT_EQ(ctx, 1, (int)single->as.tuple.items[0]->as.intLit.v);
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
  EXPECT_EQ(ctx, 2, (int)many->as.list.items[1]->as.intLit.v);
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
  // （语句位 { 为块语句（04 §10），字面量以 var 右值上下文验证）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod =
      parseSrc("var a = {}\nvar b = {1: 2}\nvar c = {1, 2}\nvar d = {k: v for k, v in m}\nvar e = {x % 3 for x in xs}",
               &arena, &diags);
  EXPECT_EQ(ctx, 5, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* emptyMap = mod->as.list.items[0]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_MAP, emptyMap->kind);
  EXPECT_EQ(ctx, 0, emptyMap->as.mapLit.count);
  AstNode* mapLit = mod->as.list.items[1]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_MAP, mapLit->kind);
  EXPECT_EQ(ctx, 1, mapLit->as.mapLit.count);
  EXPECT_EQ(ctx, 1, (int)mapLit->as.mapLit.keys[0]->as.intLit.v);
  EXPECT_EQ(ctx, 2, (int)mapLit->as.mapLit.values[0]->as.intLit.v);
  AstNode* setLit = mod->as.list.items[2]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_SET, setLit->kind);
  EXPECT_EQ(ctx, 2, setLit->as.list.count);
  AstNode* mapComp = mod->as.list.items[3]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_COMPREHENSION, mapComp->kind);
  EXPECT_EQ(ctx, AST_COMP_MAP, (int)mapComp->as.compr.compKind);
  EXPECT_EQ(ctx, AST_IDENT, mapComp->as.compr.key->kind);
  EXPECT_EQ(ctx, AST_IDENT, mapComp->as.compr.value->kind);
  EXPECT_EQ(ctx, 1, mapComp->as.compr.forCount);
  EXPECT_EQ(ctx, AST_TUPLE, mapComp->as.compr.fors[0].target->kind);  // for k, v 多变量目标
  EXPECT_EQ(ctx, 2, mapComp->as.compr.fors[0].target->as.tuple.count);
  AstNode* setComp = mod->as.list.items[4]->as.varDecl.init;
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
  AstNode* mod = parseSrc("var s = {x}", &arena, &diags);
  AstNode* set = mod->as.list.items[0]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_SET, set->kind);
  EXPECT_EQ(ctx, 1, set->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(stmtBraceIsBlockNotMap) {
  // 语句位 { 仍为块语句（04 §10）；空块语句合法且非 map
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ }", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, AST_BLOCK, mod->as.list.items[0]->kind);
  EXPECT_EQ(ctx, 0, mod->as.list.items[0]->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errTupleMissingRparen) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("(1, 2", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errComprBadTarget) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("[x for 1 in xs]", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(genexprSoleCallArg) {
  // sum(x*x for x in xs) → CALL 单实参 GEN_EXPR（裸形态，无需再括号）
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
  EXPECT_EQ(ctx, AST_OP_MUL, gen->as.compr.value->as.binary.op);
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
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprAsLogicOperand) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a && (x for x in xs)", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprAsTernaryCond) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("(x for x in xs) ? a : b", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprAsUnaryOperand) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("-(x for x in xs)", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprWithPostfix) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("(x for x in xs)[0]", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprNonSoleArg) {
  // 括号形态后跟逗号 → 1009
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f((x for x in xs), 1)", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errBareGenexprTrailingArg) {
  // 裸形态后跟更多实参 → 1001（闭括号在推导式尾部缺失）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(x for x in xs, 1)", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprSpreadArg) {
  // f(*(g for g in xs))：* 展开后的 GEN_EXPR 亦非法
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(*(g for g in xs))", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errBareGenexprSpread) {
  // f(*g for g in xs)：裸形态不得被 * 展开
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(*g for g in xs)", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprInInterp) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("\"a${x for x in xs}\"", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(mapCompTernaryValue) {
  // {k: a ? b : c for k in m} → map 推导 value 为三元（三元冒号与 map 冒号不冲突）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var d = {k: a ? b : c for k in m}", &arena, &diags);
  AstNode* comp = mod->as.list.items[0]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_COMPREHENSION, comp->kind);
  EXPECT_EQ(ctx, AST_COMP_MAP, (int)comp->as.compr.compKind);
  EXPECT_EQ(ctx, AST_TERNARY, comp->as.compr.value->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(mapSetTrailingComma) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var a = {1: 2,}\nvar b = {1, 2,}", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* m = mod->as.list.items[0]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_MAP, m->kind);
  EXPECT_EQ(ctx, 1, m->as.mapLit.count);
  AstNode* s = mod->as.list.items[1]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_SET, s->kind);
  EXPECT_EQ(ctx, 2, s->as.list.count);
  astArenaDestroy(&arena);
}

TEST(errStmtBraceMapForm) {
  // 语句位非空花括号走块语句：{ 1: 2 } 报错（块内 : 意外）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ 1: 2 }", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  astArenaDestroy(&arena);
}

TEST(errBareGenexprAfterArg) {
  // f(a, x for x in xs) → 前向非唯一实参（镜像盲区，I-1）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(a, x for x in xs)", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errParenGenexprNonSoleLast) {
  // f(a, (x for x in xs)) → 括号形态末实参亦非唯一（I-1）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(a, (x for x in xs))", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(bareGenexprWithCond) {
  // f(x for x in xs if x > 0) → 裸形态 + if 条件合法
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("f(x for x in xs if x > 0)", &arena, &diags);
  AstNode* call = expr0(mod);
  EXPECT_EQ(ctx, AST_CALL, call->kind);
  EXPECT_EQ(ctx, 1, call->as.call.count);
  EXPECT_EQ(ctx, AST_GEN_EXPR, call->as.call.args[0]->kind);
  EXPECT_EQ(ctx, 1, call->as.call.args[0]->as.compr.condCount);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(parenGenexprDoubleParen) {
  // ((x for x in xs)) → 双层括号还原为 GEN_EXPR
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var g = ((x for x in xs))", &arena, &diags);
  AstNode* decl = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_VAR_DECL, decl->kind);
  EXPECT_EQ(ctx, AST_GEN_EXPR, decl->as.varDecl.init->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errParenGenexprInInterp) {
  // "${(x for x in xs)}" → 插值段括号形态亦拒（3c 主路径）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("\"${(x for x in xs)}\"", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errGenexprAsTernaryThen) {
  // a ? (x for x in xs) : b → then 操作数亦拒（三元三操作数同拒）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a ? (x for x in xs) : b", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errBareGenexprInIndex) {
  // a[x for x in xs] → 索引位裸生成器 1009（M-2）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a[x for x in xs]", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1009, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

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
  EXPECT(ctx, fn->as.funcExpr.params[0].defaultExpr == NULL);  // T008 才有默认参数
  EXPECT(ctx, !fn->as.funcExpr.params[0].rest);
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
  EXPECT(ctx, fn->as.funcExpr.params[1].nameLen == 1 && fn->as.funcExpr.params[1].name[0] == 'b');
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

TEST(funcExprCallAndPostfix) {
  // list.map(xs, func(x) { return x + 1 }) → 调用第二实参为 FUNC_EXPR
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("list.map(xs, func(x) { return x + 1 })", &arena, &diags);
  AstNode* call = expr0(mod);
  EXPECT_EQ(ctx, AST_CALL, call->kind);
  EXPECT_EQ(ctx, 2, call->as.call.count);
  EXPECT_EQ(ctx, AST_FUNC_EXPR, call->as.call.args[1]->kind);
  EXPECT_EQ(ctx, AST_RETURN, call->as.call.args[1]->as.funcExpr.body->as.list.items[0]->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(funcExprBadParam) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func(1) { }", &arena, &diags);
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(returnMultiValue) {
  // return a, b → 多值返回即 tuple（05 §5）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func() { return a, b }", &arena, &diags);
  AstNode* fn = expr0(mod);
  AstNode* ret = fn->as.funcExpr.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_RETURN, ret->kind);
  EXPECT_EQ(ctx, AST_TUPLE, ret->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, 2, ret->as.exprStmt.expr->as.tuple.count);
  EXPECT_EQ(ctx, 0, diags.count);
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
  (void)mod;
  EXPECT(ctx, diags.count >= 1);
  EXPECT_EQ(ctx, 1001, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(returnSemicolonBare) {
  // return; → 裸 return + 显式分号（与 endSimpleStmt 的 ; 消费一致）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func() { return; n += 1 }", &arena, &diags);
  AstNode* fn = expr0(mod);
  AstNode* first = fn->as.funcExpr.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_RETURN, first->kind);
  EXPECT(ctx, first->as.exprStmt.expr == NULL);
  EXPECT_EQ(ctx, 2, fn->as.funcExpr.body->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(returnBareAtModuleLevel) {
  // 模块级裸 return：ASI 合成分号不进入值表达式（守卫含 SEMICOLON）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("return\nx", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, AST_RETURN, mod->as.list.items[0]->kind);
  EXPECT(ctx, mod->as.list.items[0]->as.exprStmt.expr == NULL);
  EXPECT_EQ(ctx, AST_IDENT, mod->as.list.items[1]->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(returnBareEolInBlock) {
  // 块内换行裸 return（prevLine 守卫主场景，回归锁定）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func() {\n  return\n}", &arena, &diags);
  AstNode* fn = expr0(mod);
  AstNode* ret = fn->as.funcExpr.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_RETURN, ret->kind);
  EXPECT(ctx, ret->as.exprStmt.expr == NULL);
  EXPECT_EQ(ctx, 1, fn->as.funcExpr.body->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(funcExprStmtFollowedByNewline) {
  // 表达式语句形态的函数表达式 + 后续语句（probe F 回归）
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("func() { }\nx", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, AST_FUNC_EXPR, mod->as.list.items[0]->as.exprStmt.expr->kind);
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
  postfixCallIndexChain(&ctx);
  callEmptyAndArgs(&ctx);
  callSpreadArg(&ctx);
  errSpreadNotLast(&ctx);
  sliceForms(&ctx);
  errCallMissingRparen(&ctx);
  ternaryThenNested(&ctx);
  commaLooserThanTernary(&ctx);
  groupingParens(&ctx);
  tupleParenForms(&ctx);
  parenDoubleGroup(&ctx);
  listLiterals(&ctx);
  listComprehension(&ctx);
  comprehensionTwoFors(&ctx);
  mapSetFiveForms(&ctx);
  singleElemSetLiteral(&ctx);
  stmtBraceIsBlockNotMap(&ctx);
  errTupleMissingRparen(&ctx);
  errComprBadTarget(&ctx);
  genexprSoleCallArg(&ctx);
  genexprAssignRhs(&ctx);
  errGenexprAsBinaryOperand(&ctx);
  errGenexprAsLogicOperand(&ctx);
  errGenexprAsTernaryCond(&ctx);
  errGenexprAsUnaryOperand(&ctx);
  errGenexprWithPostfix(&ctx);
  errGenexprNonSoleArg(&ctx);
  errBareGenexprTrailingArg(&ctx);
  errGenexprSpreadArg(&ctx);
  errBareGenexprSpread(&ctx);
  errGenexprInInterp(&ctx);
  mapCompTernaryValue(&ctx);
  mapSetTrailingComma(&ctx);
  errStmtBraceMapForm(&ctx);
  errBareGenexprAfterArg(&ctx);
  errParenGenexprNonSoleLast(&ctx);
  bareGenexprWithCond(&ctx);
  parenGenexprDoubleParen(&ctx);
  errParenGenexprInInterp(&ctx);
  errGenexprAsTernaryThen(&ctx);
  errBareGenexprInIndex(&ctx);
  funcExpression(&ctx);
  funcExprMultiParamsAndBareReturn(&ctx);
  funcExprNoParams(&ctx);
  funcExprCallAndPostfix(&ctx);
  funcExprBadParam(&ctx);
  returnMultiValue(&ctx);
  interpSegmentOrder(&ctx);
  errMemberBraceCall(&ctx);
  returnSemicolonBare(&ctx);
  returnBareAtModuleLevel(&ctx);
  returnBareEolInBlock(&ctx);
  funcExprStmtFollowedByNewline(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
