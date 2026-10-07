// T003：关键字表 / 诊断收集器 / 诊断格式化（col 按 rune 计，^ 对齐 UTF-8 安全）
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "scanner/token.h"
#include "test_harness.h"

TEST(keywordLookupHits) {
  EXPECT_EQ(ctx, TK_FUNC, msKeywordLookup("func", 4));
  EXPECT_EQ(ctx, TK_TRAIT, msKeywordLookup("trait", 5));
  EXPECT_EQ(ctx, TK_AWAIT, msKeywordLookup("await", 5));
  EXPECT_EQ(ctx, TK_NOT, msKeywordLookup("not", 3));
  EXPECT_EQ(ctx, TK_YIELD, msKeywordLookup("yield", 5));
}

TEST(keywordLookupNonKeywords) {
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("type", 4));
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("struct", 6));
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("package", 7));
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("Func", 4));   // 大小写敏感
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("变量", 6));   // 非 ASCII 标识符（6 字节）
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("funcs", 5));  // 前缀更长
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("fu", 2));     // 前缀更短
}

int main(void) {
  TestCtx ctx = {0};
  keywordLookupHits(&ctx);
  keywordLookupNonKeywords(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
