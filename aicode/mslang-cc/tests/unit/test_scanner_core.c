// T004：Scanner 核心单测（token 名表/标识符/数字/运算符/ASI/注释）
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "scanner/scanner.h"
#include "test_harness.h"

#define MAX_TOKS 256

typedef struct TokList {
  MsToken items[MAX_TOKS];
  int32_t count;
} TokList;

// 扫描到 EOF（EOF 一并入表，便于断言位置与“EOF 不补分号”）；返回 Scanner 以便检查内部态
static Scanner scanAll(const char* src, TokList* toks, MsDiagSink* diags) {
  toks->count = 0;
  Scanner s;
  scannerInit(&s, src, (int32_t)strlen(src), "t.ms", diags);
  MsToken tok;
  while (scannerNext(&s, &tok)) {
    toks->items[toks->count++] = tok;
  }
  toks->items[toks->count++] = tok;
  return s;
}

static bool tokIs(const MsToken* t, MsTokenType type, const char* text) {
  return t->type == type && (int32_t)strlen(text) == t->len && memcmp(t->start, text, (size_t)t->len) == 0;
}

TEST(tokenTypeNameMatchesEnum) {
  EXPECT_STREQ(ctx, "ID", msTokenTypeName(TK_ID));
  EXPECT_STREQ(ctx, "STAR_STAR_EQ", msTokenTypeName(TK_STAR_STAR_EQ));
  EXPECT_STREQ(ctx, "SEMICOLON", msTokenTypeName(TK_SEMICOLON));
  EXPECT_STREQ(ctx, "LARROW", msTokenTypeName(TK_LARROW));
  EXPECT_STREQ(ctx, "EOF", msTokenTypeName(TK_EOF));
}

TEST(scanIdentifierAndKeywords) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("func 变量 type", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_FUNC, "func"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_ID, "变量"));
  EXPECT_EQ(ctx, 6, toks.items[1].col);  // col 按 rune 计
  EXPECT(ctx, tokIs(&toks.items[2], TK_ID, "type"));  // struct/interface/package/type 非保留字
  EXPECT_EQ(ctx, 4, toks.count);  // + EOF
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanCommentsExposeDocLine) {
  MsDiagSink diags = {0};
  TokList toks;
  Scanner s = scanAll("/// doc\n// c\nx", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_ID, "x"));
  EXPECT_EQ(ctx, 3, toks.items[0].line);
  EXPECT_EQ(ctx, 1, s.docLine);
  EXPECT_EQ(ctx, 2, toks.count);  // x + EOF
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanBlockCommentDiagnosed) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("/* x */y", &toks, &diags);  // 数字落地前用 ID；Task 3 改回 "/* x */1" 验 INT 恢复
  EXPECT(ctx, tokIs(&toks.items[0], TK_ID, "y"));
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_BLOCK_COMMENT, (int)diags.items[0].code);
}

TEST(scanUnexpectedCharSkipped) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a # b", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_ID, "a"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_ID, "b"));
  EXPECT_EQ(ctx, 3, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_UNEXPECTED_CHAR, (int)diags.items[0].code);
}

int main(void) {
  TestCtx ctx = {0};
  tokenTypeNameMatchesEnum(&ctx);
  scanIdentifierAndKeywords(&ctx);
  scanCommentsExposeDocLine(&ctx);
  scanBlockCommentDiagnosed(&ctx);
  scanUnexpectedCharSkipped(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
