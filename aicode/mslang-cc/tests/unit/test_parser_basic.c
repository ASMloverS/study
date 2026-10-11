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
  MsDiagSink diags = {0};
  AstNode* mod = parseSrc("", &arena, &diags);
  EXPECT(ctx, mod != NULL && mod->kind == AST_MODULE);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

int main(void) {
  TestCtx ctx = {0};
  arenaAllocAndStats(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
