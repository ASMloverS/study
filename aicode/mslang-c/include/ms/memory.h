#pragma once
#include "ms/common.h"
#include "ms/value.h"
#include "ms/object.h"
#include "ms/table.h"

struct MsVM;

void* ms_reallocate(struct MsVM* vm, void* ptr, size_t old_size, size_t new_size);

/* GC API */
void ms_gc_collect(struct MsVM* vm);
void ms_gc_collect_minor(struct MsVM* vm);
void ms_mark_object(struct MsVM* vm, MsObject* obj);
void ms_mark_value(struct MsVM* vm, MsValue val);
void ms_mark_table(struct MsVM* vm, MsTable* table);

/* Write barrier: call when an old-gen object stores a young-gen reference */
void ms_write_barrier(struct MsVM* vm, MsObject* owner, MsValue val);

/* ObjectPool slab allocator */
typedef struct MsPoolSlab {
    struct MsPoolSlab* next;
    int used;
    char data[]; /* FAM: obj_size * MS_POOL_SLAB_SIZE */
} MsPoolSlab;

typedef struct {
    MsPoolSlab* slabs;
    void*       free_list;
    size_t      obj_size;
} MsObjectPool;

void  ms_pool_init(MsObjectPool* pool, size_t obj_size);
void* ms_pool_alloc(MsObjectPool* pool);
void  ms_pool_free_obj(MsObjectPool* pool, void* ptr);
void  ms_pool_destroy(MsObjectPool* pool);

#define MS_ALLOC(vm, T, count) \
    (T*)ms_reallocate((vm), NULL, 0, sizeof(T) * (size_t)(count))
#define MS_FREE(vm, T, ptr) \
    ms_reallocate((vm), (ptr), sizeof(T), 0)
#define MS_FREE_ARRAY(vm, T, ptr, count) \
    ms_reallocate((vm), (ptr), sizeof(T) * (size_t)(count), 0)
#define MS_GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)
