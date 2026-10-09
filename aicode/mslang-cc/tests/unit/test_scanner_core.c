// T004：Scanner 核心单测（token 名表/标识符/数字/运算符/ASI/注释）
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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

// token 文本去下划线后按进制求值（验证数字语义）
static uint64_t tokIntValue(const MsToken* t) {
  char buf[128];
  int32_t n = 0;
  for (int32_t i = 0; i < t->len && n < 127; i++) {
    if (t->start[i] != '_') {
      buf[n++] = t->start[i];
    }
  }
  buf[n] = '\0';
  int base = 10;
  if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
    base = 16;
  } else if (buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) {
    base = 8;
  } else if (buf[0] == '0' && buf[1] == 'b') {
    base = 2;
  }
  return strtoull(base == 10 ? buf : buf + 2, NULL, base);
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
  EXPECT_EQ(ctx, 6, toks.items[1].col);               // col 按 rune 计
  EXPECT(ctx, tokIs(&toks.items[2], TK_ID, "type"));  // struct/interface/package/type 非保留字
  EXPECT_EQ(ctx, 4, toks.count);                      // + EOF
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
  scanAll("/* x */1", &toks, &diags);  // 数字恢复路径：块注释后接 INT
  EXPECT(ctx, tokIs(&toks.items[0], TK_INT, "1"));
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

TEST(scanIntForms) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("1_000_000", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_INT, "1_000_000"));
  EXPECT_EQ(ctx, 1000000, (int)tokIntValue(&toks.items[0]));
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanIntRadix) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("0x1F 0o17 0b101", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_INT, "0x1F"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_INT, "0o17"));
  EXPECT(ctx, tokIs(&toks.items[2], TK_INT, "0b101"));
  EXPECT_EQ(ctx, 31, (int)tokIntValue(&toks.items[0]));
  EXPECT_EQ(ctx, 15, (int)tokIntValue(&toks.items[1]));
  EXPECT_EQ(ctx, 5, (int)tokIntValue(&toks.items[2]));
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanIntUppercasePrefix) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("0X1F 0O17", &toks, &diags);
  EXPECT_EQ(ctx, 31, (int)tokIntValue(&toks.items[0]));
  EXPECT_EQ(ctx, 15, (int)tokIntValue(&toks.items[1]));
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanBigintSuffix) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("123n 0xFFn", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_BIGINT, "123n"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_BIGINT, "0xFFn"));
  EXPECT_EQ(ctx, 3, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanFloatForms) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("3.14 1e9 1E-3 2.5e+10 1_000.5", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_FLOAT, "3.14"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_FLOAT, "1e9"));
  EXPECT(ctx, tokIs(&toks.items[2], TK_FLOAT, "1E-3"));
  EXPECT(ctx, tokIs(&toks.items[3], TK_FLOAT, "2.5e+10"));
  EXPECT(ctx, tokIs(&toks.items[4], TK_FLOAT, "1_000.5"));
  EXPECT_EQ(ctx, 6, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanIntOverflowToBigint) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("9223372036854775808", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_BIGINT, "9223372036854775808"));
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_INT_OVERFLOW, (int)diags.items[0].code);
}

TEST(scanNumericErrors) {
  struct {
    const char* src;
    MsTokenType type;
    const char* text;
    int code;
  } cases[] = {
      {"123abc", TK_INT, "123", MSE_INT_TRAILING},  {"1e", TK_FLOAT, "1e", MSE_FLOAT_EXPONENT},
      {"1e+", TK_FLOAT, "1e+", MSE_FLOAT_EXPONENT}, {".5", TK_FLOAT, ".5", MSE_FLOAT_LEADING_DOT},
      {"1_", TK_INT, "1_", MSE_UNDERSCORE},         {"1__0", TK_INT, "1__0", MSE_UNDERSCORE},
      {"0xG", TK_INT, "0x", MSE_BASE_PREFIX},       {"0o8", TK_INT, "0o", MSE_DIGIT_BASE},
  };
  for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
    MsDiagSink diags = {0};
    TokList toks;
    scanAll(cases[i].src, &toks, &diags);
    EXPECT(ctx, tokIs(&toks.items[0], cases[i].type, cases[i].text));
    EXPECT_EQ(ctx, 1, diags.count);
    EXPECT_EQ(ctx, cases[i].code, (int)diags.items[0].code);
  }
}

TEST(scanOperatorMaxMunch) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a **= b x := y <- ch", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_ID, "a"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_STAR_STAR_EQ, "**="));
  EXPECT(ctx, tokIs(&toks.items[2], TK_ID, "b"));
  EXPECT(ctx, tokIs(&toks.items[3], TK_ID, "x"));
  EXPECT(ctx, tokIs(&toks.items[4], TK_COLON_EQ, ":="));
  EXPECT(ctx, tokIs(&toks.items[5], TK_ID, "y"));
  EXPECT(ctx, tokIs(&toks.items[6], TK_LARROW, "<-"));
  EXPECT(ctx, tokIs(&toks.items[7], TK_ID, "ch"));
  EXPECT_EQ(ctx, 9, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanOperatorMatrix) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll(
      "+ += - -= -> * *= ** **= / /= % %= = == ! != < <= << <<= > >= >> >>= "
      "& && &= | || |= ^ ^= ~ ? : := . , ; ( ) [ ] { }",
      &toks, &diags);
  static const MsTokenType types[] = {
      TK_PLUS,         TK_PLUS_EQ, TK_MINUS,    TK_MINUS_EQ, TK_ARROW,      TK_STAR,      TK_STAR_EQ, TK_STAR_STAR,
      TK_STAR_STAR_EQ, TK_SLASH,   TK_SLASH_EQ, TK_PERCENT,  TK_PERCENT_EQ, TK_EQ,        TK_EQ_EQ,   TK_BANG,
      TK_BANG_EQ,      TK_LT,      TK_LT_EQ,    TK_LT_LT,    TK_LT_LT_EQ,   TK_GT,        TK_GT_EQ,   TK_GT_GT,
      TK_GT_GT_EQ,     TK_AMP,     TK_AMP_AMP,  TK_AMP_EQ,   TK_PIPE,       TK_PIPE_PIPE, TK_PIPE_EQ, TK_CARET,
      TK_CARET_EQ,     TK_TILDE,   TK_QUESTION, TK_COLON,    TK_COLON_EQ,   TK_DOT,       TK_COMMA,   TK_SEMICOLON,
      TK_LPAREN,       TK_RPAREN,  TK_LBRACKET, TK_RBRACKET, TK_LBRACE,     TK_RBRACE,
  };
  EXPECT_EQ(ctx, (int)(sizeof(types) / sizeof(types[0])), toks.count - 1);  // 46 运算符 + EOF
  EXPECT_EQ(ctx, 0, diags.count);
  for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, TK_EOF, toks.items[toks.count - 1].type);
}

TEST(scanTrailingDotRecovers) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("5.", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_INT, "5"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_DOT, "."));
  EXPECT_EQ(ctx, 3, toks.count);  // INT DOT EOF
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_FLOAT_TRAILING_DOT, (int)diags.items[0].code);
}

TEST(scanMemberAccessExemptFromDotDiag) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("5.toString", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_INT, "5"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_DOT, "."));
  EXPECT(ctx, tokIs(&toks.items[2], TK_ID, "toString"));
  EXPECT_EQ(ctx, 4, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(asiBasic) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a\nb", &toks, &diags);
  EXPECT_EQ(ctx, TK_ID, toks.items[0].type);
  EXPECT(ctx, tokIs(&toks.items[1], TK_SEMICOLON, ""));
  EXPECT_EQ(ctx, 1, toks.items[1].line);
  EXPECT_EQ(ctx, 2, toks.items[1].col);  // ; 定位在换行符处
  EXPECT_EQ(ctx, TK_ID, toks.items[2].type);
  EXPECT_EQ(ctx, TK_EOF, toks.items[3].type);  // EOF 不补分号
  EXPECT_EQ(ctx, 4, toks.count);
}

TEST(asiParenSuppress) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("f(\na\n)", &toks, &diags);
  EXPECT_EQ(ctx, 5, toks.count);  // f ( a ) EOF，括号内换行不产生分号（细则 1）
  EXPECT_EQ(ctx, TK_RPAREN, toks.items[3].type);
  EXPECT_EQ(ctx, TK_EOF, toks.items[4].type);
}

TEST(asiLarrowSuppress) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("ch <-\n1", &toks, &diags);
  EXPECT_EQ(ctx, 4, toks.count);  // ch <- 1 EOF，行尾 <- 抑制（细则 3）
  EXPECT_EQ(ctx, TK_LARROW, toks.items[1].type);
  EXPECT_EQ(ctx, TK_INT, toks.items[2].type);
}

TEST(asiReturnBreaksStatement) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("return\nx", &toks, &diags);
  EXPECT_EQ(ctx, TK_RETURN, toks.items[0].type);
  EXPECT(ctx, tokIs(&toks.items[1], TK_SEMICOLON, ""));
  EXPECT_EQ(ctx, 7, toks.items[1].col);  // return 后换行立即断句（细则 4）
  EXPECT_EQ(ctx, TK_ID, toks.items[2].type);
  EXPECT_EQ(ctx, 4, toks.count);
}

TEST(asiLiteralAndKeywordTriggers) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("true\nnil\nthis\n1.5\n2n\nbreak\ncontinue\nthrow\ndefer\nyield", &toks, &diags);
  EXPECT_EQ(ctx, 20, toks.count);  // 10 token + 9 ; + EOF（结尾无换行）
  for (int i = 0; i < 9; i++) {
    EXPECT_EQ(ctx, TK_SEMICOLON, toks.items[2 * i + 1].type);
  }
  EXPECT_EQ(ctx, TK_YIELD, toks.items[18].type);
  EXPECT_EQ(ctx, TK_EOF, toks.items[19].type);
}

TEST(asiClosingBracketTriggers) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a[1]\nb\n{\nc\n}\n", &toks, &diags);
  static const MsTokenType types[] = {
      TK_ID,        TK_LBRACKET, TK_INT, TK_RBRACKET, TK_SEMICOLON, TK_ID,
      TK_SEMICOLON, TK_LBRACE,   TK_ID,  TK_RBRACE,   TK_SEMICOLON, TK_EOF,
  };
  EXPECT_EQ(ctx, 12, toks.count);
  for (int i = 0; i < 12; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
}

TEST(asiParenDepthMixed) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("x = ([1])\ny", &toks, &diags);
  static const MsTokenType types[] = {
      TK_ID, TK_EQ, TK_LPAREN, TK_LBRACKET, TK_INT, TK_RBRACKET, TK_RPAREN, TK_SEMICOLON, TK_ID, TK_EOF,
  };
  EXPECT_EQ(ctx, 10, toks.count);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 10, toks.items[7].col);  // ; 位于 ([1]) 之后的换行处
}

TEST(asiOperatorNoSemicolon) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a +\nb", &toks, &diags);
  EXPECT_EQ(ctx, 4, toks.count);  // a + b EOF，行尾运算符不触发
  EXPECT_EQ(ctx, TK_PLUS, toks.items[1].type);
}

int main(void) {
  TestCtx ctx = {0};
  tokenTypeNameMatchesEnum(&ctx);
  scanIdentifierAndKeywords(&ctx);
  scanCommentsExposeDocLine(&ctx);
  scanBlockCommentDiagnosed(&ctx);
  scanUnexpectedCharSkipped(&ctx);
  scanIntForms(&ctx);
  scanIntRadix(&ctx);
  scanIntUppercasePrefix(&ctx);
  scanBigintSuffix(&ctx);
  scanFloatForms(&ctx);
  scanIntOverflowToBigint(&ctx);
  scanNumericErrors(&ctx);
  scanOperatorMaxMunch(&ctx);
  scanOperatorMatrix(&ctx);
  scanTrailingDotRecovers(&ctx);
  scanMemberAccessExemptFromDotDiag(&ctx);
  asiBasic(&ctx);
  asiParenSuppress(&ctx);
  asiLarrowSuppress(&ctx);
  asiReturnBreaksStatement(&ctx);
  asiLiteralAndKeywordTriggers(&ctx);
  asiClosingBracketTriggers(&ctx);
  asiParenDepthMixed(&ctx);
  asiOperatorNoSemicolon(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
