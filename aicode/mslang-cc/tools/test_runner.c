// golden fixture 跑器（T002）：拉起 ms 子进程执行 .ms 用例，捕获 stdout/退出码与期望文件比对
// 契约见 docs/tasks/T002-test-infra.md §2：<case>.ms + 同名 .out（比对前 LF 归一化）；
// 可选 .code（期望退出码，缺省 0）、.err（期望 stderr 首行前缀）、.timeout（秒，缺省 10）
// 自测（tests/unit/test_runner_selftest.c）以 MS_TEST_RUNNER_NO_MAIN 屏蔽驱动段后包含本文件
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// stdout/stderr 捕获上限：防失控用例无限输出撑爆内存，超限截断后比对自然 FAIL
#define RUNNER_MAX_OUTPUT (1024 * 1024)

static char* slurpFile(const char* path, size_t* outLen) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    return NULL;
  }
  size_t cap = 4096;
  size_t len = 0;
  char* data = malloc(cap);
  if (data == NULL) {
    fclose(file);
    return NULL;
  }
  for (;;) {
    if (len == cap) {
      cap *= 2;
      char* grown = realloc(data, cap);
      if (grown == NULL) {
        free(data);
        fclose(file);
        return NULL;
      }
      data = grown;
    }
    size_t got = fread(data + len, 1, cap - len, file);
    len += got;
    if (got == 0) {
      break;
    }
  }
  fclose(file);
  char* shrunk = realloc(data, len + 1);
  if (shrunk == NULL) {
    free(data);
    return NULL;
  }
  shrunk[len] = '\0';
  *outLen = len;
  return shrunk;
}

// 就地 CRLF -> LF（孤立 \r 保留）；要求 NUL 结尾
// 局部名用 src/dst：独立构建时本 TU 会引入 <unistd.h>（Task 4），read/write 会与 POSIX 同名符号冲突
static void normalizeLf(char* text) {
  char* src = text;
  char* dst = text;
  while (*src != '\0') {
    if (src[0] == '\r' && src[1] == '\n') {
      src++;
    }
    *dst++ = *src++;
  }
  *dst = '\0';
}

// 首个差异行号（1 起，按 \n 切行：尾随 \n 计一空行）；相等返回 0
static int firstDiffLine(const char* expected, const char* actual) {
  const char* lhs = expected;
  const char* rhs = actual;
  int line = 1;
  for (;;) {
    const char* lhsEnd = strchr(lhs, '\n');
    const char* rhsEnd = strchr(rhs, '\n');
    size_t lhsLen = lhsEnd == NULL ? strlen(lhs) : (size_t)(lhsEnd - lhs);
    size_t rhsLen = rhsEnd == NULL ? strlen(rhs) : (size_t)(rhsEnd - rhs);
    if (lhsLen != rhsLen || memcmp(lhs, rhs, lhsLen) != 0) {
      return line;
    }
    if ((lhsEnd == NULL) != (rhsEnd == NULL)) {
      return line + 1;
    }
    if (lhsEnd == NULL) {
      return 0;
    }
    lhs = lhsEnd + 1;
    rhs = rhsEnd + 1;
    line++;
  }
}

static const char* lineAt(const char* text, int line) {
  if (*text == '\0') {
    return NULL;
  }
  int current = 1;
  for (const char* p = text; *p != '\0'; p++) {
    if (current == line) {
      return p;
    }
    if (*p == '\n') {
      current++;
    }
  }
  return current == line ? text + strlen(text) : NULL;
}

static void printDiffLine(FILE* stream, const char* label, const char* text, int line) {
  const char* p = lineAt(text, line);
  if (p == NULL) {
    fprintf(stream, "  %s: (no such line)\n", label);
    return;
  }
  const char* end = strchr(p, '\n');
  int len = end == NULL ? (int)strlen(p) : (int)(end - p);
  fprintf(stream, "  %s: %.*s\n", label, len, p);
}

// 输出首个差异行（行号 + 双方内容）
static void printDiff(FILE* stream, const char* expected, const char* actual) {
  int line = firstDiffLine(expected, actual);
  if (line == 0) {
    return;
  }
  fprintf(stream, "  first difference at line %d:\n", line);
  printDiffLine(stream, "expected", expected, line);
  printDiffLine(stream, "actual  ", actual, line);
}

// 读单整数文件（.code/.timeout）；文件缺失或无法解析返回 false，调用方保留缺省值
static bool readOptionalInt(const char* path, int* value) {
  size_t len = 0;
  char* text = slurpFile(path, &len);
  if (text == NULL) {
    return false;
  }
  char* end = NULL;
  long parsed = strtol(text, &end, 10);
  bool ok = end != text;
  free(text);
  if (ok) {
    *value = (int)parsed;
  }
  return ok;
}

// 驱动段（子进程拉起/比对/入口）由 Task 4 填入，纯逻辑到此为止
#ifndef MS_TEST_RUNNER_NO_MAIN
#endif
