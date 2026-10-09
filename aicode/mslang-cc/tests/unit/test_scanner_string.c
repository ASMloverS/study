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

TEST(stringEscapeFullTable) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"\\n\\r\\t\\\\\\\"\\'\\0\\x41\\u{4E2D}\"", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[1], TK_STR_PART, "\\n\\r\\t\\\\\\\"\\'\\0\\x41\\u{4E2D}"));
  EXPECT_EQ(ctx, 4, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringEscapeMalformed) {
  struct {
    const char* src;
    int code;
  } cases[] = {
      {"\"\\q\"", MSE_ESCAPE},
      {"\"\\x4\"", MSE_ESCAPE},
      {"\"\\xG1\"", MSE_ESCAPE},
      {"\"\\u\"", MSE_ESCAPE},
      {"\"\\u12\"", MSE_ESCAPE},
      {"\"\\u{}\"", MSE_ESCAPE},
      {"\"\\u{1234567}\"", MSE_ESCAPE},
      {"\"\\u{110000}\"", MSE_ESCAPE_VALUE},
      {"\"\\u{D800}\"", MSE_ESCAPE_VALUE},
      {"\"\\u{DFFF}\"", MSE_ESCAPE_VALUE},
  };
  for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
    MsDiagSink diags = {0};
    TokList toks;
    scanAll(cases[i].src, &toks, &diags);
    EXPECT(ctx, toks.count >= 3);  // STRING [STR_PART] STRING EOF 结构完整
    EXPECT_EQ(ctx, 1, diags.count);
    EXPECT_EQ(ctx, cases[i].code, (int)diags.items[0].code);
  }
}

TEST(stringUnterminatedNewlineRecovers) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"abc\nx", &toks, &diags);
  static const MsTokenType types[] = {TK_STRING, TK_STR_PART, TK_STRING, TK_SEMICOLON, TK_ID, TK_EOF};
  EXPECT_EQ(ctx, 6, toks.count);
  for (int i = 0; i < 6; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_STRING_UNTERMINATED, (int)diags.items[0].code);
}

TEST(stringUnterminatedEof) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"abc", &toks, &diags);
  static const MsTokenType types[] = {TK_STRING, TK_STR_PART, TK_STRING, TK_EOF};
  EXPECT_EQ(ctx, 4, toks.count);
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_STRING_UNTERMINATED, (int)diags.items[0].code);
}

TEST(stringInterpSplit) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a = ${a}, sum = ${a + b}\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING,   TK_STR_PART,     TK_INTERP_BEGIN, TK_ID,     TK_INTERP_END,
      TK_STR_PART, TK_INTERP_BEGIN, TK_ID,           TK_PLUS,   TK_ID,
      TK_INTERP_END, TK_STRING,     TK_EOF,
  };
  EXPECT_EQ(ctx, 13, toks.count);
  for (int i = 0; i < 13; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT(ctx, tokIs(&toks.items[1], TK_STR_PART, "a = "));
  EXPECT(ctx, tokIs(&toks.items[5], TK_STR_PART, ", sum = "));
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpNewlineNoAsi) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"${1 +\n2}b\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_INTERP_BEGIN, TK_INT, TK_PLUS, TK_INT, TK_INTERP_END,
      TK_STR_PART, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 9, toks.count);  // 插值内换行无 ;（01 §7 细则 2）
  for (int i = 0; i < 9; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT(ctx, tokIs(&toks.items[6], TK_STR_PART, "b"));
  EXPECT_EQ(ctx, 2, toks.items[6].line);  // 段起点跟随插值内换行
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpBraceMatching) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"${ {1: 2} }\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_INTERP_BEGIN, TK_LBRACE, TK_INT, TK_COLON, TK_INT, TK_RBRACE,
      TK_INTERP_END, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 10, toks.count);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpNested) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"${\"in${x}\"}\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_INTERP_BEGIN, TK_STRING, TK_STR_PART, TK_INTERP_BEGIN, TK_ID,
      TK_INTERP_END, TK_STRING, TK_INTERP_END, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 11, toks.count);
  for (int i = 0; i < 11; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpUnterminatedEof) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a${x", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_STR_PART, TK_INTERP_BEGIN, TK_ID, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 6, toks.count);
  for (int i = 0; i < 6; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 2, diags.count);  // 插值域 + 字符串域各一
  EXPECT_EQ(ctx, MSE_STRING_UNTERMINATED, (int)diags.items[0].code);
  EXPECT_EQ(ctx, MSE_STRING_UNTERMINATED, (int)diags.items[1].code);
}

TEST(stringInterpDepthCapped) {
  MsDiagSink diags = {0};
  TokList toks;
  char src[MS_LEX_MAX_DEPTH * 4 + 64];
  int32_t n = 0;
  for (int32_t i = 0; i < MS_LEX_MAX_DEPTH + 1; i++) {
    memcpy(src + n, "\"${", 3);
    n += 3;
  }
  memcpy(src + n, "x", 1);
  n += 1;
  for (int32_t i = 0; i < MS_LEX_MAX_DEPTH + 1; i++) {
    memcpy(src + n, "}\"", 2);
    n += 2;
  }
  src[n] = '\0';
  scanAll(src, &toks, &diags);
  bool sawDepth = false;
  for (int32_t i = 0; i < diags.count; i++) {
    sawDepth = sawDepth || diags.items[i].code == MSE_LEX_DEPTH;
  }
  EXPECT(ctx, sawDepth);
  EXPECT(ctx, toks.count >= 2);
}

TEST(rawStringPreservedVerbatim) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("`a\\t${x}\\n`", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_RAW_STR, "`a\\t${x}\\n`"));  // 字节级原样（含反引号）
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(rawStringMultilineBytes) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("`a\nb` x", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_RAW_STR, "`a\nb`"));
  EXPECT_EQ(ctx, 6, toks.items[0].len);  // 6 字节：跨行换行原样保留
  EXPECT(ctx, tokIs(&toks.items[1], TK_ID, "x"));
  EXPECT_EQ(ctx, 2, toks.items[1].line);
  EXPECT_EQ(ctx, 4, toks.items[1].col);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(rawStringUnterminatedEof) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("`abc", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_RAW_STR, "`abc"));
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_RAW_UNTERMINATED, (int)diags.items[0].code);
}

TEST(rawStringAsiAfterEnd) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("`a`\nx", &toks, &diags);
  EXPECT_EQ(ctx, TK_RAW_STR, toks.items[0].type);
  EXPECT_EQ(ctx, TK_SEMICOLON, toks.items[1].type);
  EXPECT_EQ(ctx, TK_ID, toks.items[2].type);
  EXPECT_EQ(ctx, 4, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

int main(void) {
  TestCtx ctx = {0};
  stringEmptyAndPlain(&ctx);
  stringDollarWithoutBraceIsContent(&ctx);
  stringMultibyteColumns(&ctx);
  stringAdjacentNotConcatenated(&ctx);
  stringAsiAfterEnd(&ctx);
  stringEscapeFullTable(&ctx);
  stringEscapeMalformed(&ctx);
  stringUnterminatedNewlineRecovers(&ctx);
  stringUnterminatedEof(&ctx);
  stringInterpSplit(&ctx);
  stringInterpNewlineNoAsi(&ctx);
  stringInterpBraceMatching(&ctx);
  stringInterpNested(&ctx);
  stringInterpUnterminatedEof(&ctx);
  stringInterpDepthCapped(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
