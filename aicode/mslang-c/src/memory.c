#include "ms/memory.h"
#include "ms/vm.h"
#include <stdlib.h>

void* ms_reallocate(struct MsVM* vm, void* ptr, size_t old_size, size_t new_size) {
    if (vm) {
        vm->bytes_allocated += new_size;
        if (old_size <= vm->bytes_allocated)
            vm->bytes_allocated -= old_size;
        else
            vm->bytes_allocated = 0;
        if (new_size > old_size && vm->bytes_allocated > vm->next_gc)
            ms_gc_collect(vm);
    }
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    void* result = realloc(ptr, new_size);
    if (!result) abort();
    return result;
}
