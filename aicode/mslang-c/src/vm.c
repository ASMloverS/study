#include "ms/vm.h"
#include <stdlib.h>

void ms_vm_init(MsVM* vm) {
    vm->objects = NULL;
    ms_table_init(&vm->strings);
}

void ms_vm_free(MsVM* vm) {
    MsObject* obj = vm->objects;
    while (obj) {
        MsObject* next = obj->next;
        ms_object_free(vm, obj);
        obj = next;
    }
    vm->objects = NULL;
    ms_table_free(&vm->strings);
}
