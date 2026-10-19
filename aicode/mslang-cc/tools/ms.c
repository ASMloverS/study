#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "common/common.h"
#include "compiler/compiler.h"
#include "parser/ast.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
#include "vm/vm.h"

static void printUsage(const char* program) {
  fprintf(stderr, "usage: %s <command> [args]\n", program);
  fprintf(stderr, "commands:\n");
  fprintf(stderr, "  version                  print version\n");
  fprintf(stderr, "  run <file.ms> [--trace]  execute script (same as ms <file.ms>)\n");
  fprintf(stderr, "  --trace                  step-execute instruction log (20 §6)\n");
  fprintf(stderr, "  --dump-tokens <file.ms>  dump token stream (debug)\n");
  fprintf(stderr, "  --dump-ast <file.ms>     dump annotated AST (debug)\n");
  fprintf(stderr, "  dump <file.ms>           disassemble bytecode (debug)\n");
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

// 前端：scan → parse → bind（T009）；返回模块树（诊断在 sink；失败为部分树）
static AstNode* frontEnd(const char* src, int32_t len, const char* path, AstArena* arena, MsDiagSink* diags) {
  Parser p;
  parserInit(&p, src, len, path, arena, diags);
  AstNode* mod = parserParseModule(&p);
  if (mod != NULL) {
    Binder binder;
    binderInit(&binder, arena, path, diags);
    binderBind(&binder, mod);
    binderDestroy(&binder);
  }
  return mod;
}

static void printDiags(const MsDiagSink* diags, const char* src) {
  for (int32_t i = 0; i < diags->count; i++) {
    char buf[512];
    msDiagFormat(&diags->items[i], src, buf, sizeof(buf));
    fputs(buf, stderr);
  }
  if (diags->overflow > 0) {
    fprintf(stderr, "ms: %d more diagnostics suppressed\n", (int)diags->overflow);
  }
}

// run（T011 接入执行）：前端 + 编译 + VM；诊断 stderr；编译错 3、运行时错误 1（27 §10）
static int runScript(const char* path, bool trace) {
  size_t len = 0;
  char* src = slurpFile(path, &len);
  if (src == NULL) {
    fprintf(stderr, "ms: cannot read %s\n", path);
    return 2;
  }
  len = normalizeLf(src);
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = frontEnd(src, (int32_t)len, path, &arena, &diags);
  int exitCode = 0;
  if (mod == NULL) {
    exitCode = 3;
  } else {
    Chunk chunk;
    bool ok = compilerCompile(mod, &chunk, path, &diags);
    // 常量文本与 chunk 名借用 src：执行完成前不可释放
    if (ok) {
      MsVM vm;
      msVmInit(&vm);
      vm.trace = trace ? stdout : NULL;
      msVmRun(&vm, &chunk);
      if (vm.hasError) {
        fprintf(stderr, "%s:%d: %s\n", path, vm.errLine > 0 ? vm.errLine : 0, vm.errMsg);
        exitCode = 1;
      }
      msVmFree(&vm);
    } else {
      exitCode = 3;
    }
    chunkFree(&chunk);
  }
  printDiags(&diags, src);
  astArenaDestroy(&arena);
  free(src);
  return exitCode;
}

// --dump-ast：规范化标注 AST 到 stdout，诊断 stderr；有错退出码 1
static int dumpAst(const char* path) {
  size_t len = 0;
  char* src = slurpFile(path, &len);
  if (src == NULL) {
    fprintf(stderr, "ms: cannot read %s\n", path);
    return 2;
  }
  len = normalizeLf(src);
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = frontEnd(src, (int32_t)len, path, &arena, &diags);
  if (mod != NULL) {
    astDumpModule(mod, stdout);
  }
  printDiags(&diags, src);
  astArenaDestroy(&arena);
  free(src);
  return diags.count > 0 ? 1 : 0;
}

// dump（T010）：前端 + 编译 → 反汇编到 stdout；诊断 stderr、退出码同 --dump-ast
static int dumpBytecode(const char* path) {
  size_t len = 0;
  char* src = slurpFile(path, &len);
  if (src == NULL) {
    fprintf(stderr, "ms: cannot read %s\n", path);
    return 2;
  }
  len = normalizeLf(src);
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = frontEnd(src, (int32_t)len, path, &arena, &diags);
  if (mod != NULL) {
    Chunk chunk;
    compilerCompile(mod, &chunk, path, &diags);
    // 常量文本与 chunk 名借用 src：disasm 完成前不可释放
    compilerDisasm(&chunk, stdout);
    chunkFree(&chunk);
  }
  printDiags(&diags, src);
  astArenaDestroy(&arena);
  free(src);
  return diags.count > 0 ? 1 : 0;
}

int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "version") == 0) {
    printf("mslang %s\n", MS_VERSION);
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--dump-tokens") == 0 && isMsPath(argv[2])) {
    return dumpTokens(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "--dump-ast") == 0 && isMsPath(argv[2])) {
    return dumpAst(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "dump") == 0 && isMsPath(argv[2])) {
    return dumpBytecode(argv[2]);
  }
  // run 子命令与直接执行（27 §1）：run <file.ms> [--trace] / ms <file.ms> [--trace]
  int argStart = 1;
  if (argc >= 2 && strcmp(argv[1], "run") == 0) {
    argStart = 2;
  }
  if (argc > argStart) {
    const char* path = NULL;
    bool trace = false;
    bool usageErr = false;
    for (int i = argStart; i < argc; i++) {
      if (strcmp(argv[i], "--trace") == 0) {
        trace = true;
      } else if (isMsPath(argv[i]) && path == NULL) {
        path = argv[i];
      } else {
        usageErr = true;
      }
    }
    if (path != NULL && !usageErr) {
      return runScript(path, trace);
    }
  }
  printUsage(argv[0]);
  return 1;
}
