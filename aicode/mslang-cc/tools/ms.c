#include <stdio.h>
#include <string.h>

#include "common/common.h"

static void printUsage(const char *program) {
  fprintf(stderr, "usage: %s <command> [args]\n", program);
  fprintf(stderr, "commands:\n");
  fprintf(stderr, "  version    print version\n");
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "version") == 0) {
    printf("mslang %s\n", MS_VERSION);
    return 0;
  }
  printUsage(argv[0]);
  return 1;
}
