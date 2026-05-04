#include "ms/memory.h"
#include "ms/vm.h"
#include "ms/consts.h"
#include <stdlib.h>

void* ms_reallocate(struct MsVM* vm, void* ptr, size_t old_size, size_t new_size) {
    if (vm) {
        vm->bytes_allocated += new_size;
        if (old_size <= vm->bytes_allocated)
            vm->bytes_allocated -= old_size;
        else
            vm->bytes_allocated = 0;
#ifdef MSLANG_VM_STATS
        if (vm->bytes_allocated > vm->stats.bytes_allocated_peak)
            vm->stats.bytes_allocated_peak = vm->bytes_allocated;
#endif
        if (new_size > old_size) {
            if (vm->gc_phase != MS_GC_IDLE)
                ms_gc_incremental_step(vm);
            else if (vm->bytes_allocated > vm->next_gc)
                ms_gc_incremental_step(vm);
        }
    }
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    void* result = realloc(ptr, new_size);
    if (!result) abort();
    return result;
}

/* ---- Write barrier ---- */

void ms_write_barrier(struct MsVM* vm, MsObject* owner, MsValue val) {
    if (!owner || owner->generation != 1) return;
    if (!MS_IS_OBJECT(val)) return;
    MsObject* child = MS_AS_OBJECT(val);
    if (child->generation != 0) return;
    if (owner->in_remembered_set) return;
    owner->in_remembered_set = true;
    if (vm->remembered_count >= vm->remembered_capacity) {
        int new_cap = vm->remembered_capacity < 8 ? 8 : vm->remembered_capacity * 2;
        vm->remembered_set = (MsObject**)realloc(vm->remembered_set,
                              sizeof(MsObject*) * (size_t)new_cap);
        if (!vm->remembered_set) abort();
        vm->remembered_capacity = new_cap;
    }
    vm->remembered_set[vm->remembered_count++] = owner;
}

/* ---- ObjectPool slab allocator ---- */

void ms_pool_init(MsObjectPool* pool, size_t obj_size) {
    pool->slabs     = NULL;
    pool->free_list = NULL;
    pool->obj_size  = obj_size < sizeof(void*) ? sizeof(void*) : obj_size;
}

void* ms_pool_alloc(MsObjectPool* pool) {
    if (pool->free_list) {
        void* p = pool->free_list;
        pool->free_list = *(void**)p;
        return p;
    }
    /* use remaining slots in current slab if available */
    if (pool->slabs && pool->slabs->used < MS_POOL_SLAB_SIZE) {
        MsPoolSlab* slab = pool->slabs;
        void* p = slab->data + (size_t)slab->used * pool->obj_size;
        slab->used++;
        return p;
    }
    /* allocate new slab */
    size_t slab_data = pool->obj_size * (size_t)MS_POOL_SLAB_SIZE;
    MsPoolSlab* slab = (MsPoolSlab*)malloc(sizeof(MsPoolSlab) + slab_data);
    if (!slab) abort();
    slab->used = 1;
    slab->next = pool->slabs;
    pool->slabs = slab;
    return slab->data;
}

void ms_pool_free_obj(MsObjectPool* pool, void* ptr) {
    *(void**)ptr = pool->free_list;
    pool->free_list = ptr;
}

void ms_pool_destroy(MsObjectPool* pool) {
    MsPoolSlab* slab = pool->slabs;
    while (slab) {
        MsPoolSlab* next = slab->next;
        free(slab);
        slab = next;
    }
    pool->slabs     = NULL;
    pool->free_list = NULL;
}
