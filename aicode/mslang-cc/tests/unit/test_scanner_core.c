// T004：Scanner 核心单测（token 名表/标识符/数字/运算符/ASI/注释）
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "scanner/token.h"
#include "test_harness.h"

TEST(tokenTypeNameMatchesEnum) {
  EXPECT_STREQ(ctx, "ID", msTokenTypeName(TK_ID));
  EXPECT_STREQ(ctx, "STAR_STAR_EQ", msTokenTypeName(TK_STAR_STAR_EQ));
  EXPECT_STREQ(ctx, "SEMICOLON", msTokenTypeName(TK_SEMICOLON));
  EXPECT_STREQ(ctx, "LARROW", msTokenTypeName(TK_LARROW));
  EXPECT_STREQ(ctx, "EOF", msTokenTypeName(TK_EOF));
}

int main(void) {
  TestCtx ctx = {0};
  tokenTypeNameMatchesEnum(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
