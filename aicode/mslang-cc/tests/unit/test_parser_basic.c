// T006：Parser 声明/基础语句单测（docs/tasks/T006 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "parser/parser.h"
#include "test_harness.h"

// 解析整模块：返回 AST_MODULE 根（调用方负责 arena 销毁）
static AstNode* parseSrc(const char* src, AstArena* arena, MsDiagSink* diags) {
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", arena, diags);
  return parserParseModule(&p);
}

TEST(arenaAllocAndStats) {
  AstArena arena;
  astArenaInit(&arena);
  EXPECT_EQ(ctx, 0, (int)astArenaBytesUsed(&arena));
  EXPECT_EQ(ctx, 0, arena.nodeCount);
  AstNode* node = astNewNode(&arena, AST_MODULE, 3, 7);
  EXPECT_EQ(ctx, AST_MODULE, node->kind);
  EXPECT_EQ(ctx, 3, node->line);
  EXPECT_EQ(ctx, 7, node->col);
  EXPECT(ctx, node->as.list.items == NULL);  // 载荷清零
  EXPECT_EQ(ctx, 1, arena.nodeCount);
  EXPECT(ctx, astArenaBytesUsed(&arena) >= sizeof(AstNode));
  EXPECT(ctx, astArenaAlloc(&arena, 100000) != NULL);  // 超大分配单独成块
  astArenaDestroy(&arena);
  EXPECT_EQ(ctx, 0, (int)astArenaBytesUsed(&arena));
  EXPECT_EQ(ctx, 0, arena.nodeCount);
}

TEST(parseEmptyModule) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("", &arena, &diags);
  EXPECT(ctx, mod != NULL && mod->kind == AST_MODULE);
  EXPECT_EQ(ctx, 0, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(parseExprStmtLiterals) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x\n42\n3.5\ntrue\nnil", &arena, &diags);
  EXPECT_EQ(ctx, 5, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode** s = mod->as.list.items;
  EXPECT_EQ(ctx, AST_EXPR_STMT, s[0]->kind);
  EXPECT_EQ(ctx, AST_IDENT, s[0]->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, AST_INT, s[1]->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, 42, (int)s[1]->as.exprStmt.expr->as.intLit.v);
  EXPECT_EQ(ctx, AST_FLOAT, s[2]->as.exprStmt.expr->kind);
  EXPECT_F64_EQ(ctx, 3.5, s[2]->as.exprStmt.expr->as.floatLit.v);
  EXPECT_EQ(ctx, AST_BOOL, s[3]->as.exprStmt.expr->kind);
  EXPECT(ctx, s[3]->as.exprStmt.expr->as.boolLit.b);
  EXPECT_EQ(ctx, AST_NIL, s[4]->as.exprStmt.expr->kind);
  astArenaDestroy(&arena);
}

TEST(parseIntRadixForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("0x1F\n0o17\n0b101\n1_000", &arena, &diags);
  EXPECT_EQ(ctx, 4, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  int expect[] = {31, 15, 5, 1000};
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(ctx, expect[i], (int)mod->as.list.items[i]->as.exprStmt.expr->as.intLit.v);
  }
  astArenaDestroy(&arena);
}

TEST(parseBigintAndRune) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("99999999999999999999n\n'a'", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  AstNode* big = mod->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIGINT, big->kind);
  EXPECT_EQ(ctx, 20, big->as.bigintLit.len);  // n 后缀已剥离
  EXPECT(ctx, memcmp(big->as.bigintLit.start, "99999999999999999999", 20) == 0);
  AstNode* rune = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_RUNE, rune->kind);
  EXPECT_EQ(ctx, 97, (int)rune->as.runeLit.cp);
  astArenaDestroy(&arena);
}

TEST(parseNodePositions) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x\n  y", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 1, mod->as.list.items[0]->line);
  EXPECT_EQ(ctx, 1, mod->as.list.items[0]->col);
  EXPECT_EQ(ctx, 2, mod->as.list.items[1]->line);
  EXPECT_EQ(ctx, 3, mod->as.list.items[1]->col);  // col 按 rune 计
  astArenaDestroy(&arena);
}

TEST(parseStringLiterals) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("\"a${x}b\"\n\"\"\n`raw`", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* s = mod->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_STRING, s->kind);
  EXPECT_EQ(ctx, 3, s->as.strLit.count);
  EXPECT(ctx, s->as.strLit.segs[0].expr == NULL);  // 字面段 "a"
  EXPECT_EQ(ctx, 1, s->as.strLit.segs[0].len);
  EXPECT_EQ(ctx, 'a', s->as.strLit.segs[0].start[0]);
  EXPECT_EQ(ctx, AST_IDENT, s->as.strLit.segs[1].expr->kind);  // 插值段
  EXPECT(ctx, s->as.strLit.segs[2].expr == NULL);  // 字面段 "b"
  EXPECT_EQ(ctx, 'b', s->as.strLit.segs[2].start[0]);
  AstNode* empty = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_STRING, empty->kind);
  EXPECT_EQ(ctx, 0, empty->as.strLit.count);
  AstNode* raw = mod->as.list.items[2]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_STRING, raw->kind);
  EXPECT(ctx, raw->as.strLit.raw);
  EXPECT_EQ(ctx, 1, raw->as.strLit.count);
  EXPECT_EQ(ctx, 3, raw->as.strLit.segs[0].len);  // 反引号已剥（"raw" 3 字节）
  astArenaDestroy(&arena);
}

TEST(parseBareSemicolonsSilent) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x;\n;\n42", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);  // 孤立/尾随 ; 均静默消费（04 §10）
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(parseUnterminatedBlock) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ x", &arena, &diags);
  EXPECT_EQ(ctx, 0, mod->as.list.count);  // 块失败被同步跳过
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(parseStrayRbraceRecovers) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x }\ny\n42", &arena, &diags);  // 模块层游离 }
  EXPECT_EQ(ctx, 3, mod->as.list.count);  // x/y/42 均保留，} 不再挂死
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_UNEXPECTED, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(parseUnterminatedInterp) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("\"a${x", &arena, &diags);  // scanner 自恢复闭串，parser 报 1001
  EXPECT_EQ(ctx, 0, mod->as.list.count);
  EXPECT_EQ(ctx, 3, diags.count);  // scanner 2 条（未闭合域）+ parser 1 条
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, (int)diags.items[2].code);
  astArenaDestroy(&arena);
}

TEST(parseErrorThenRecover) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("+\ny\n42", &arena, &diags);  // 首句错误；sync 以换行为界，y/42 均恢复（04 §10）
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_UNEXPECTED, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(parseIndexMemberPostfix) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("xs[0]\nobj.f\nm[0].f", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* idx = mod->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_INDEX, idx->kind);
  EXPECT_EQ(ctx, AST_IDENT, idx->as.indexExpr.obj->kind);
  EXPECT_EQ(ctx, AST_INT, idx->as.indexExpr.index->kind);
  AstNode* mem = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_MEMBER, mem->kind);
  EXPECT_EQ(ctx, AST_IDENT, mem->as.memberExpr.obj->kind);
  EXPECT_EQ(ctx, 1, mem->as.memberExpr.nameLen);  // f
  EXPECT_EQ(ctx, 'f', mem->as.memberExpr.name[0]);
  AstNode* mix = mod->as.list.items[2]->as.exprStmt.expr;  // 后缀链左结合
  EXPECT_EQ(ctx, AST_MEMBER, mix->kind);
  EXPECT_EQ(ctx, AST_INDEX, mix->as.memberExpr.obj->kind);
  astArenaDestroy(&arena);
}

TEST(parseVarDeclForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var y\nvar x = 10\nconst MAX = 100", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* d0 = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_VAR_DECL, d0->kind);
  EXPECT(ctx, d0->as.varDecl.init == NULL);  // 无初值默认 nil（04 §1）
  EXPECT_EQ(ctx, 1, d0->as.varDecl.nameLen);
  EXPECT_EQ(ctx, 'y', d0->as.varDecl.name[0]);
  AstNode* d1 = mod->as.list.items[1];
  EXPECT_EQ(ctx, 0, d1->as.varDecl.flags);
  EXPECT_EQ(ctx, AST_INT, d1->as.varDecl.init->kind);
  EXPECT_EQ(ctx, 10, (int)d1->as.varDecl.init->as.intLit.v);
  AstNode* d2 = mod->as.list.items[2];
  EXPECT_EQ(ctx, AST_VAR_CONST, d2->as.varDecl.flags);
  EXPECT_EQ(ctx, 3, d2->as.varDecl.nameLen);  // MAX
  astArenaDestroy(&arena);
}

TEST(parseShortDeclInBlock) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ x := 20 }", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* block = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_BLOCK, block->kind);
  AstNode* decl = block->as.list.items[0];
  EXPECT_EQ(ctx, AST_VAR_DECL, decl->kind);
  EXPECT_EQ(ctx, AST_VAR_SHORT, decl->as.varDecl.flags);
  EXPECT_EQ(ctx, 20, (int)decl->as.varDecl.init->as.intLit.v);
  astArenaDestroy(&arena);
}

TEST(parseBlockScopingShadowOk) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ var x = 1 }\nvar x = 2", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);  // 不同作用域遮蔽合法（04 §1）
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errShortDeclAtModuleTop) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x := 1", &arena, &diags);
  EXPECT_EQ(ctx, 0, mod->as.list.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_SHORT_TOPLEVEL, (int)diags.items[0].code);
  EXPECT_EQ(ctx, 1, diags.items[0].line);
  astArenaDestroy(&arena);
}

TEST(errDuplicateDeclSameScope) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ var x\nx := 1 }", &arena, &diags);  // 04 §1 示例
  (void)mod;
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DUP_DECL, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errDuplicateVarVarSameScope) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var x = 1\nvar x = 2", &arena, &diags);
  (void)mod;
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DUP_DECL, (int)diags.items[0].code);
  EXPECT_EQ(ctx, 2, diags.items[0].line);
  astArenaDestroy(&arena);
}

TEST(parseCompoundAssign) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x += 1", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* a = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_ASSIGN, a->kind);  // 复合形态保留，编译期展开（T006 §2）
  EXPECT_EQ(ctx, AST_OP_ADD, a->as.assign.op);
  EXPECT_EQ(ctx, AST_IDENT, a->as.assign.target->kind);
  EXPECT_EQ(ctx, 1, (int)a->as.assign.value->as.intLit.v);
  astArenaDestroy(&arena);
}

TEST(parseIndexMemberAssign) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("xs[0] = v\nobj.f = v", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, AST_INDEX, mod->as.list.items[0]->as.assign.target->kind);
  EXPECT_EQ(ctx, AST_MEMBER, mod->as.list.items[1]->as.assign.target->kind);
  EXPECT_EQ(ctx, AST_OP_NONE, mod->as.list.items[0]->as.assign.op);
  astArenaDestroy(&arena);
}

TEST(parseTupleSwapAssign) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a, b = b, a", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* a = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_ASSIGN, a->kind);
  EXPECT_EQ(ctx, AST_TUPLE, a->as.assign.target->kind);  // 目标元组
  EXPECT_EQ(ctx, 2, a->as.assign.target->as.tuple.count);
  EXPECT_EQ(ctx, AST_TUPLE, a->as.assign.value->kind);  // 右值元组
  EXPECT_EQ(ctx, 2, a->as.assign.value->as.tuple.count);
  EXPECT_EQ(ctx, 'a', a->as.assign.target->as.tuple.items[0]->as.ident.name[0]);
  EXPECT_EQ(ctx, 'b', a->as.assign.target->as.tuple.items[1]->as.ident.name[0]);
  astArenaDestroy(&arena);
}

TEST(parseStarDestructure) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a, *mid, z = xs", &arena, &diags);  // 04 §2
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* d = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_DESTRUCTURE, d->kind);
  EXPECT_EQ(ctx, 3, d->as.destructure.count);
  EXPECT_EQ(ctx, 1, d->as.destructure.restIndex);  // * 可在任意位置（04 §2）
  EXPECT_EQ(ctx, 'm', d->as.destructure.targets[1]->as.ident.name[0]);
  EXPECT_EQ(ctx, AST_IDENT, d->as.destructure.value->kind);
  astArenaDestroy(&arena);
}

TEST(errDestructureTwoStars) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a, *b, *c = xs", &arena, &diags);
  (void)mod;
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DESTRUCT_STARS, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errBadAssignTarget) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("1 = 2", &arena, &diags);
  (void)mod;
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_BAD_TARGET, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(parseComparisonExpr) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var r = x == y", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* init = mod->as.list.items[0]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_BINARY, init->kind);
  EXPECT_EQ(ctx, AST_OP_EQ, init->as.binary.op);
  EXPECT_EQ(ctx, AST_IDENT, init->as.binary.lhs->kind);
  EXPECT_EQ(ctx, AST_IDENT, init->as.binary.rhs->kind);
  astArenaDestroy(&arena);
}

TEST(errShortDeclBadTarget) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ xs[0] := 1 }", &arena, &diags);
  (void)mod;
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_BAD_TARGET, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(parseMultilineDeclTerminator) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var x =\n5\nvar y = 6", &arena, &diags);  // 跨行初值后正确断句
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 5, (int)mod->as.list.items[0]->as.varDecl.init->as.intLit.v);
  astArenaDestroy(&arena);
}

TEST(errSameLineStmtsInsideBlock) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ x := 1 y := 2 }", &arena, &diags);  // 同行无分隔 → 报错
  (void)mod;
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(parseIfElseChain) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("if a { var x = 1 } else if b { var y = 2 } else { var z = 3 }",
                          &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* ifa = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_IF, ifa->kind);
  EXPECT_EQ(ctx, AST_IDENT, ifa->as.ifStmt.cond->kind);
  EXPECT_EQ(ctx, AST_BLOCK, ifa->as.ifStmt.thenBlock->kind);
  EXPECT_EQ(ctx, 1, ifa->as.ifStmt.thenBlock->as.list.count);
  AstNode* ifb = ifa->as.ifStmt.elseNode;
  EXPECT_EQ(ctx, AST_IF, ifb->kind);  // else if → 嵌套 IF（04 §3）
  EXPECT_EQ(ctx, AST_BLOCK, ifb->as.ifStmt.thenBlock->kind);
  EXPECT_EQ(ctx, AST_BLOCK, ifb->as.ifStmt.elseNode->kind);
  EXPECT_EQ(ctx, 1, ifb->as.ifStmt.elseNode->as.list.count);
  EXPECT(ctx, ifb->as.ifStmt.elseNode->as.list.items[0]->as.varDecl.name[0] == 'z');
  astArenaDestroy(&arena);
}

TEST(errIfConditionParenthesized) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("if (x) {}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_IF_PAREN, (int)diags.items[0].code);
  EXPECT_EQ(ctx, 1, mod->as.list.count);  // 就地恢复：if 仍完整产出
  EXPECT_EQ(ctx, AST_IF, mod->as.list.items[0]->kind);
  EXPECT_EQ(ctx, AST_IDENT, mod->as.list.items[0]->as.ifStmt.cond->kind);
  EXPECT_EQ(ctx, AST_BLOCK, mod->as.list.items[0]->as.ifStmt.thenBlock->kind);
  astArenaDestroy(&arena);
}

TEST(parseMultiErrorRecovery) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x := 1\nif (y) {}\n1 = 2", &arena, &diags);
  EXPECT_EQ(ctx, 3, diags.count);  // 一次编译收集多条（DoD §3）
  EXPECT_EQ(ctx, MSE_SYN_SHORT_TOPLEVEL, (int)diags.items[0].code);
  EXPECT_EQ(ctx, MSE_SYN_IF_PAREN, (int)diags.items[1].code);
  EXPECT_EQ(ctx, MSE_SYN_BAD_TARGET, (int)diags.items[2].code);
  EXPECT_EQ(ctx, 1, mod->as.list.count);  // 中间 if 恢复成功
  EXPECT_EQ(ctx, AST_IF, mod->as.list.items[0]->kind);
  astArenaDestroy(&arena);
}

TEST(errElseOnNewLine) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("if a {\n}\nelse {}", &arena, &diags);  // Go 风格：else 须同行
  EXPECT_EQ(ctx, 2, diags.count);  // else 无表达式起点 + 其 } 成游离括号（sync 停留，模块层上报）
  EXPECT_EQ(ctx, MSE_SYN_UNEXPECTED, (int)diags.items[0].code);
  EXPECT_EQ(ctx, MSE_SYN_UNEXPECTED, (int)diags.items[1].code);
  EXPECT_EQ(ctx, 1, mod->as.list.count);  // 第一条 if（无 else）保留
  astArenaDestroy(&arena);
}

TEST(errChainedComparison) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("if x < y < z {}", &arena, &diags);  // 03 优先级表：不可链式
  (void)mod;
  EXPECT_EQ(ctx, 2, diags.count);  // 链式比较为主错；失败 if 的孤儿 } 由模块层上游离括号
  EXPECT_EQ(ctx, MSE_SYN_CHAINED_CMP, (int)diags.items[0].code);
  EXPECT_EQ(ctx, MSE_SYN_UNEXPECTED, (int)diags.items[1].code);
  astArenaDestroy(&arena);
}

TEST(parseBlockErrorRecoversByLine) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ 1 2\nvar y = 3 }", &arena, &diags);  // 块内错误按行恢复
  AstNode* block = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_BLOCK, block->kind);
  EXPECT_EQ(ctx, 1, block->as.list.count);  // var y = 3 保留
  EXPECT_EQ(ctx, 1, diags.count);
  astArenaDestroy(&arena);
}

TEST(arenaStatsAfterParse) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var x = 1\nif a { var y }", &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 7, arena.nodeCount);  // module+var+int+if+ident+block+var（DoD §4）
  EXPECT(ctx, astArenaBytesUsed(&arena) >= sizeof(AstNode) * 7);
  astArenaDestroy(&arena);
}

int main(void) {
  TestCtx ctx = {0};
  arenaAllocAndStats(&ctx);
  parseEmptyModule(&ctx);
  parseExprStmtLiterals(&ctx);
  parseIntRadixForms(&ctx);
  parseBigintAndRune(&ctx);
  parseNodePositions(&ctx);
  parseStringLiterals(&ctx);
  parseBareSemicolonsSilent(&ctx);
  parseUnterminatedBlock(&ctx);
  parseStrayRbraceRecovers(&ctx);
  parseUnterminatedInterp(&ctx);
  parseErrorThenRecover(&ctx);
  parseIndexMemberPostfix(&ctx);
  parseVarDeclForms(&ctx);
  parseShortDeclInBlock(&ctx);
  parseBlockScopingShadowOk(&ctx);
  errShortDeclAtModuleTop(&ctx);
  errDuplicateDeclSameScope(&ctx);
  errDuplicateVarVarSameScope(&ctx);
  parseCompoundAssign(&ctx);
  parseIndexMemberAssign(&ctx);
  parseTupleSwapAssign(&ctx);
  parseStarDestructure(&ctx);
  errDestructureTwoStars(&ctx);
  errBadAssignTarget(&ctx);
  parseComparisonExpr(&ctx);
  errShortDeclBadTarget(&ctx);
  parseMultilineDeclTerminator(&ctx);
  errSameLineStmtsInsideBlock(&ctx);
  parseIfElseChain(&ctx);
  errIfConditionParenthesized(&ctx);
  parseMultiErrorRecovery(&ctx);
  errElseOnNewLine(&ctx);
  errChainedComparison(&ctx);
  parseBlockErrorRecoversByLine(&ctx);
  arenaStatsAfterParse(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
