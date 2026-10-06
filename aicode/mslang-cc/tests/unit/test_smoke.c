// 断言宏族冒烟自测（T002 §5.1）：正常路径 failures==0；失败路径计数与 file:line 消息格式
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "test_harness.h"

#if defined(_WIN32)
#include <io.h>
#define SMOKE_DUP(fd) _dup(fd)
#define SMOKE_DUP2(from, to) _dup2(from, to)
#define SMOKE_CLOSE(fd) _close(fd)
#define SMOKE_FILENO(stream) _fileno(stream)
static FILE* smokeReopen(const char* path, const char* mode, FILE* stream) {
  FILE* reopened = NULL;
  return freopen_s(&reopened, path, mode, stream) == 0 ? reopened : NULL;
}
#define SMOKE_REOPEN(path, mode, stream) smokeReopen(path, mode, stream)
#else
#include <unistd.h>
#define SMOKE_DUP(fd) dup(fd)
#define SMOKE_DUP2(from, to) dup2(from, to)
#define SMOKE_CLOSE(fd) close(fd)
#define SMOKE_FILENO(stream) fileno(stream)
#define SMOKE_REOPEN(path, mode, stream) freopen(path, mode, stream)
#endif

// 捕获 fn 写往 stderr 的文本（dup + freopen + dup2 恢复）；outCap 含结尾 NUL
static bool captureStderr(void (*fn)(TestCtx*), TestCtx* ctx, char* out, size_t outCap) {
  const char* capturePath = "test_smoke_capture.tmp";
  if (outCap == 0) {
    return false;
  }
  fflush(stderr);
  int saved = SMOKE_DUP(SMOKE_FILENO(stderr));
  if (saved < 0) {
    return false;
  }
  if (SMOKE_REOPEN(capturePath, "wb", stderr) == NULL) {
    SMOKE_CLOSE(saved);
    return false;
  }
  fn(ctx);
  fflush(stderr);
  SMOKE_DUP2(saved, SMOKE_FILENO(stderr));
  SMOKE_CLOSE(saved);
  FILE* file = fopen(capturePath, "rb");
  size_t got = 0;
  if (file != NULL) {
    got = fread(out, 1, outCap - 1, file);
    fclose(file);
  }
  remove(capturePath);
  out[got] = '\0';
  return file != NULL;
}

TEST(expectPasses) {
  EXPECT(ctx, 1 == 1);
  EXPECT(ctx, true);
  EXPECT_EQ(ctx, 42, 42);
  EXPECT_F64_EQ(ctx, 0.1 + 0.2, 0.3);
  EXPECT_STREQ(ctx, "abc", "abc");
}

static void expectEqFailureBody(TestCtx* ctx) {
  EXPECT_EQ(ctx, 1, 2);
  EXPECT(ctx, 2 == 2);
}

TEST(expectEqFailureReports) {
  TestCtx inner = {0};
  char log[512];
  EXPECT(ctx, captureStderr(expectEqFailureBody, &inner, log, sizeof(log)));
  EXPECT_EQ(ctx, 1, inner.failures);
  EXPECT(ctx, strstr(log, "test_smoke.c:") != NULL);
  EXPECT(ctx, strstr(log, "expected 1 got 2") != NULL);
}

TEST(otherFailuresCounted) {
  TestCtx inner = {0};
  EXPECT(&inner, 1 == 2);
  EXPECT_F64_EQ(&inner, 1.0, 2.0);
  EXPECT_STREQ(&inner, "a", "b");
  EXPECT_EQ(ctx, 3, inner.failures);
}

int main(void) {
  TestCtx ctx = {0};
  expectPasses(&ctx);
  expectEqFailureReports(&ctx);
  otherFailuresCounted(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
