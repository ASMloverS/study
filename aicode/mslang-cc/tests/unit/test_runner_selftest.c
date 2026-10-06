// 跑器纯逻辑自测（T002 §5.1）：以 MS_TEST_RUNNER_NO_MAIN 屏蔽驱动段后包含被测源码
#define MS_TEST_RUNNER_NO_MAIN
#include "../../tools/test_runner.c"

#include "test_harness.h"

#define SELFTEST_TMP "test_runner_selftest.tmp"

TEST(normalizeLfStripsCr) {
  char crlf[] = "a\r\nb\r\n";
  normalizeLf(crlf);
  EXPECT_STREQ(ctx, "a\nb\n", crlf);
  char lone[] = "a\rb";
  normalizeLf(lone);
  EXPECT_STREQ(ctx, "a\rb", lone);
}

TEST(firstDiffLineFinds) {
  EXPECT_EQ(ctx, 0, firstDiffLine("a\nb", "a\nb"));
  EXPECT_EQ(ctx, 2, firstDiffLine("a\nb\nc", "a\nx\nc"));
  EXPECT_EQ(ctx, 1, firstDiffLine("", "x"));
  EXPECT_EQ(ctx, 3, firstDiffLine("a\nb\n", "a\nb"));
  EXPECT_EQ(ctx, 2, firstDiffLine("a\nb", "a\nbb"));
}

TEST(printDiffMarksFirstLine) {
  FILE* file = tmpfile();
  EXPECT(ctx, file != NULL);
  if (file == NULL) {
    return;
  }
  printDiff(file, "alpha\nbeta\ngamma", "alpha\nBETA\ngamma");
  rewind(file);
  char log[512];
  size_t got = fread(log, 1, sizeof(log) - 1, file);
  log[got] = '\0';
  fclose(file);
  EXPECT(ctx, strstr(log, "line 2") != NULL);
  EXPECT(ctx, strstr(log, "beta") != NULL);
  EXPECT(ctx, strstr(log, "BETA") != NULL);
}

TEST(slurpReadsBinaryExact) {
  FILE* file = fopen(SELFTEST_TMP, "wb");
  EXPECT(ctx, file != NULL);
  if (file == NULL) {
    return;
  }
  EXPECT(ctx, fwrite("x\r\ny\0z", 1, 7, file) == 7);
  fclose(file);
  size_t len = 0;
  char* text = slurpFile(SELFTEST_TMP, &len);
  remove(SELFTEST_TMP);
  EXPECT(ctx, text != NULL);
  if (text == NULL) {
    return;
  }
  EXPECT(ctx, len == 7 && memcmp(text, "x\r\ny\0z", 7) == 0);
  free(text);
  EXPECT(ctx, slurpFile("definitely_missing_file.abc", &len) == NULL);
}

TEST(readOptionalIntParses) {
  FILE* file = fopen(SELFTEST_TMP, "wb");
  EXPECT(ctx, file != NULL);
  if (file == NULL) {
    return;
  }
  fprintf(file, "42\n");
  fclose(file);
  int value = 0;
  EXPECT(ctx, readOptionalInt(SELFTEST_TMP, &value));
  EXPECT_EQ(ctx, 42, value);
  // MSVC 适配：赋值形 fopen 触发 C4996，改声明即初始化形（同上两处）保持告警干净
  FILE* again = fopen(SELFTEST_TMP, "wb");
  fprintf(again, "bad");
  fclose(again);
  EXPECT(ctx, !readOptionalInt(SELFTEST_TMP, &value));
  EXPECT_EQ(ctx, 42, value);
  remove(SELFTEST_TMP);
  EXPECT(ctx, !readOptionalInt(SELFTEST_TMP, &value));
}

int main(void) {
  TestCtx ctx = {0};
  normalizeLfStripsCr(&ctx);
  firstDiffLineFinds(&ctx);
  printDiffMarksFirstLine(&ctx);
  slurpReadsBinaryExact(&ctx);
  readOptionalIntParses(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
