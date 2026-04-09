#include "ms/common.h"
#include "ms/consts.h"

int main(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("mslang-c %s\n", MS_VERSION);
        return 0;
    }
    fprintf(stderr, "Usage: mslang-c [--version] [<script.ms>]\n");
    return 1;
}
