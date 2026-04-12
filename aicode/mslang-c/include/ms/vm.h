#pragma once
#include "ms/object.h"
#include "ms/table.h"

typedef struct MsVM {
    MsObject* objects;   // GC intrusive list head
    MsTable   strings;
} MsVM;

void ms_vm_init(MsVM* vm);
void ms_vm_free(MsVM* vm);
