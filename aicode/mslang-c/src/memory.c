#include "ms/memory.h"
#include "ms/vm.h"
#include <stdlib.h>

void* ms_reallocate(struct MsVM* vm, void* ptr, size_t old_size, size_t new_size) {
    MS_UNUSED(vm);
    MS_UNUSED(old_size);
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    void* result = realloc(ptr, new_size);
    if (!result) abort();
    return result;
}
