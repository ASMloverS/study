#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/common.h"
#include "scanner/scanner.h"

static void printUsage(const char* program) {
  fprintf(stderr, "usage: %s <command> [args]\n", program);
  fprintf(stderr, "commands:\n");
  fprintf(stderr, "  version                  print version\n");
  fprintf(stderr, "  --dump-tokens <file.ms>  dump token stream (debug)\n");
}

static bool isMsPath(const char* path) {
  size_t len = strlen(path);
  return len > 3 && strcmp(path + len - 3, ".ms") == 0;
}

// 读取整个文件到 NUL 结尾缓冲；失败返回 NULL
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

// 就地 \r\n / \r → \n（01-lexical.md：读取层归一化，扫描器只见 \n）；返回归一化后长度
static size_t normalizeLf(char* text) {
  char* src = text;
  char* dst = text;
  while (*src != '\0') {
    if (*src == '\r') {
      src++;
      if (*src == '\n') {
        src++;
      }
      *dst++ = '\n';
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
  return (size_t)(dst - text);
}

static void printToken(const MsToken* tok) {
  printf("%d:%d %s", tok->line, tok->col, msTokenTypeName(tok->type));
  if (tok->len > 0) {
    printf(" %.*s", (int)tok->len, tok->start);
  }
  printf("\n");
}

// --dump-tokens：token 流到 stdout，诊断到 stderr；有错退出码 1
static int dumpTokens(const char* path) {
  size_t len = 0;
  char* src = slurpFile(path, &len);
  if (src == NULL) {
    fprintf(stderr, "ms: cannot read %s\n", path);
    return 2;
  }
  len = normalizeLf(src);
  MsDiagSink diags = {0};
  Scanner s;
  scannerInit(&s, src, (int32_t)len, path, &diags);
  MsToken tok;
  while (scannerNext(&s, &tok)) {
    printToken(&tok);
  }
  printToken(&tok);  // EOF 行
  for (int32_t i = 0; i < diags.count; i++) {
    char buf[512];
    msDiagFormat(&diags.items[i], src, buf, sizeof(buf));
    fputs(buf, stderr);
  }
  if (diags.overflow > 0) {
    fprintf(stderr, "ms: %d more diagnostics suppressed\n", (int)diags.overflow);
  }
  free(src);
  return diags.count > 0 ? 1 : 0;
}

// run 占位（T002）：T005 起先做词法检查——诊断打印 stderr、退出码 3（27 §10 编译错误；真实执行 T011 接入）
static int runScript(const char* path) {
  size_t len = 0;
  char* src = slurpFile(path, &len);
  if (src == NULL) {
    fprintf(stderr, "ms: cannot read %s\n", path);
    return 2;
  }
  len = normalizeLf(src);
  MsDiagSink diags = {0};
  Scanner s;
  scannerInit(&s, src, (int32_t)len, path, &diags);
  MsToken tok;
  while (scannerNext(&s, &tok)) {
  }
  for (int32_t i = 0; i < diags.count; i++) {
    char buf[512];
    msDiagFormat(&diags.items[i], src, buf, sizeof(buf));
    fputs(buf, stderr);
  }
  free(src);
  return diags.count > 0 ? 3 : 0;
}

int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "version") == 0) {
    printf("mslang %s\n", MS_VERSION);
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--dump-tokens") == 0 && isMsPath(argv[2])) {
    return dumpTokens(argv[2]);
  }
  // run 占位（T002）：T005 起先做词法检查（词法错即编译错）；T011 起替换为真实执行
  if (argc == 2 && isMsPath(argv[1])) {
    return runScript(argv[1]);
  }
  printUsage(argv[0]);
  return 1;
}
