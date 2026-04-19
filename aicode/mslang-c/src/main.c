#include "ms/common.h"
#include "ms/consts.h"
#include "ms/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_file(const char* path) {
    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f) { fprintf(stderr, "Cannot open file '%s'.\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[read] = '\0';
    return buf;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("mslang-c %s\n", MS_VERSION);
        return 0;
    }
    if (argc == 2) {
        char* src = read_file(argv[1]);
        if (!src) return 1;
        MsVM vm;
        ms_vm_init(&vm);
        MsInterpretResult result = ms_vm_interpret(&vm, src, argv[1]);
        ms_vm_free(&vm);
        free(src);
        return result == MS_INTERPRET_OK ? 0 : 1;
    }
    fprintf(stderr, "Usage: mslang-c [--version] [<script.ms>]\n");
    return 1;
}
