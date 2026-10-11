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
  AstNode* mod = parseSrc("+\ny\n42", &arena, &diags);  // 首句错误，sync 吞至 ; （y 被吞），42 保留
  EXPECT_EQ(ctx, 1, mod->as.list.count);
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
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
