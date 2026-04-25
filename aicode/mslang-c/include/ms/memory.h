#pragma once
#include "ms/common.h"
#include "ms/value.h"
#include "ms/object.h"
#include "ms/table.h"

struct MsVM;

void* ms_reallocate(struct MsVM* vm, void* ptr, size_t old_size, size_t new_size);

/* GC API */
void ms_gc_collect(struct MsVM* vm);
void ms_mark_object(struct MsVM* vm, MsObject* obj);
void ms_mark_value(struct MsVM* vm, MsValue val);
void ms_mark_table(struct MsVM* vm, MsTable* table);

#define MS_ALLOC(vm, T, count) \
    (T*)ms_reallocate((vm), NULL, 0, sizeof(T) * (size_t)(count))
#define MS_FREE(vm, T, ptr) \
    ms_reallocate((vm), (ptr), sizeof(T), 0)
#define MS_FREE_ARRAY(vm, T, ptr, count) \
    ms_reallocate((vm), (ptr), sizeof(T) * (size_t)(count), 0)
#define MS_GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)
