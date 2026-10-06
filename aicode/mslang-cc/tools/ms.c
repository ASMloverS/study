#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "common/common.h"

static void printUsage(const char* program) {
  fprintf(stderr, "usage: %s <command> [args]\n", program);
  fprintf(stderr, "commands:\n");
  fprintf(stderr, "  version    print version\n");
}

static bool isMsPath(const char* path) {
  size_t len = strlen(path);
  return len > 3 && strcmp(path + len - 3, ".ms") == 0;
}

int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "version") == 0) {
    printf("mslang %s\n", MS_VERSION);
    return 0;
  }
  // run 占位（T002）：让 golden 用例可跑通（注释脚本期望空输出）；T011 起替换为真实执行
  if (argc == 2 && isMsPath(argv[1])) {
    return 0;
  }
  printUsage(argv[0]);
  return 1;
}
