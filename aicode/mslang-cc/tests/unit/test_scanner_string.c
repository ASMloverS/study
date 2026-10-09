// T005：字符串/插值/原始串/rune 单测（01-lexical.md §3.3 §3.4 §7 细则 2）
#include <stdbool.h>
#include <string.h>

#include "scanner/scanner.h"
#include "test_harness.h"

#define MAX_TOKS 256

typedef struct TokList {
  MsToken items[MAX_TOKS];
  int32_t count;
} TokList;

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

TEST(stringEmptyAndPlain) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"\" \"abc\"", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_STRING, "\""));
  EXPECT(ctx, tokIs(&toks.items[1], TK_STRING, "\""));
  EXPECT(ctx, tokIs(&toks.items[2], TK_STRING, "\""));
  EXPECT(ctx, tokIs(&toks.items[3], TK_STR_PART, "abc"));
  EXPECT(ctx, tokIs(&toks.items[4], TK_STRING, "\""));
  EXPECT_EQ(ctx, 6, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringDollarWithoutBraceIsContent) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a$b$\"", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[1], TK_STR_PART, "a$b$"));
  EXPECT_EQ(ctx, 4, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringMultibyteColumns) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"\xe4\xb8\xad\xe6\x96\x87\" x", &toks, &diags);  // "中文" x
  EXPECT(ctx, tokIs(&toks.items[1], TK_STR_PART, "\xe4\xb8\xad\xe6\x96\x87"));
  EXPECT(ctx, tokIs(&toks.items[3], TK_ID, "x"));
  EXPECT_EQ(ctx, 6, toks.items[3].col);  // col 按 rune 计（01 §8）："=1 中=2 文=3 "=4 空格=5
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringAdjacentNotConcatenated) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a\" \"b\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_STR_PART, TK_STRING, TK_STRING, TK_STR_PART, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 7, toks.count);
  for (int i = 0; i < 7; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringAsiAfterEnd) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a\"\nx", &toks, &diags);
  EXPECT_EQ(ctx, TK_SEMICOLON, toks.items[3].type);
  EXPECT_EQ(ctx, TK_ID, toks.items[4].type);
  EXPECT_EQ(ctx, 6, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

int main(void) {
  TestCtx ctx = {0};
  stringEmptyAndPlain(&ctx);
  stringDollarWithoutBraceIsContent(&ctx);
  stringMultibyteColumns(&ctx);
  stringAdjacentNotConcatenated(&ctx);
  stringAsiAfterEnd(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
