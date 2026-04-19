#pragma once
#include "ms/object.h"

typedef struct {
    int  line, column;
    char message[256];
} MsDiagnostic;

typedef struct MsVM MsVM;

MsObjFunction* ms_compile(MsVM* vm, const char* source, const char* path,
                           MsDiagnostic* diags, int* diag_count, int max_diags);
