// T003：关键字表 / 诊断收集器 / 诊断格式化（col 按 rune 计，^ 对齐 UTF-8 安全）
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
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("", 0));       // 空输入
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("fu\0c", 4));  // 内嵌 NUL 不误配
}

TEST(diagSinkStoresAndOverflows) {
  MsDiagSink sink = {0};
  for (int i = 0; i < 25; i++) {
    MsDiagnostic diag = {MS_DIAG_ERROR, (uint32_t)i, "f.ms", 1, 1, 1, "m"};
    msDiagAdd(&sink, &diag);
  }
  EXPECT_EQ(ctx, 20, sink.count);
  EXPECT_EQ(ctx, 5, sink.overflow);
  EXPECT_EQ(ctx, 0, sink.items[0].code);
  EXPECT_EQ(ctx, 19, sink.items[19].code);
}

TEST(diagFormatMatchesDesign) {
  MsDiagnostic diag = {MS_DIAG_ERROR, 1, "f.ms", 2, 5, 3, "msg"};
  const char* source = "line one\nlet msg here ok\nline three\n";
  char out[256];
  msDiagFormat(&diag, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "f.ms:2:5: error[MSE0001]: msg\nlet msg here ok\n    ^^^\n", out);
}

TEST(diagFormatMultibyteColIsRuneBased) {
  // "a 中 b"：中占 3 字节（rune 列 3）；col=4 指向 b 前的空格，^ 缩进按 rune 列而非字节列
  MsDiagnostic diag = {MS_DIAG_ERROR, 2, "f.ms", 2, 4, 1, "bad"};
  const char* source = "x = 1\na 中 b\n";
  char out[256];
  msDiagFormat(&diag, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "f.ms:2:4: error[MSE0002]: bad\na 中 b\n   ^\n", out);
}

TEST(diagFormatEdgeCases) {
  const char* source = "first line\n";
  char out[256];
  // len<1 时 ^ 最少 1 个；warning 级别小写
  MsDiagnostic zeroLen = {MS_DIAG_WARNING, 100, "w.ms", 1, 1, 0, "wmsg"};
  msDiagFormat(&zeroLen, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "w.ms:1:1: warning[MSE0100]: wmsg\nfirst line\n^\n", out);
  // 无源码：仅头部行
  MsDiagnostic noSrc = {MS_DIAG_ERROR, 3, "s.ms", 9, 9, 9, "no src"};
  msDiagFormat(&noSrc, NULL, out, sizeof(out));
  EXPECT_STREQ(ctx, "s.ms:9:9: error[MSE0003]: no src\n", out);
  // 行号越界：仅头部行
  msDiagFormat(&noSrc, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "s.ms:9:9: error[MSE0003]: no src\n", out);
  // 源以换行结尾时，lines+1 行视为越界（尾部空行不存在）
  MsDiagnostic tailMiss = {MS_DIAG_ERROR, 5, "t.ms", 2, 1, 1, "tail"};
  msDiagFormat(&tailMiss, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "t.ms:2:1: error[MSE0005]: tail\n", out);
  // 缓冲不足：截断且 NUL 结尾、头部前缀保留
  MsDiagnostic diag = {MS_DIAG_ERROR, 4, "f.ms", 1, 1, 1, "trunc"};
  char tiny[8];
  msDiagFormat(&diag, source, tiny, sizeof(tiny));
  EXPECT(ctx, strlen(tiny) < sizeof(tiny));
  EXPECT(ctx, strncmp(tiny, "f.ms:1", 6) == 0);
}

int main(void) {
  TestCtx ctx = {0};
  keywordLookupHits(&ctx);
  keywordLookupNonKeywords(&ctx);
  diagSinkStoresAndOverflows(&ctx);
  diagFormatMatchesDesign(&ctx);
  diagFormatMultibyteColIsRuneBased(&ctx);
  diagFormatEdgeCases(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
