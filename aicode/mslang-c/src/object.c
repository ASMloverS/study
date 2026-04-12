#include "ms/object.h"
#include "ms/vm.h"
#include "ms/memory.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MsObject* ms_alloc_object(struct MsVM* vm, size_t size, MsObjectType type) {
    MsObject* obj = (MsObject*)ms_reallocate(vm, NULL, 0, size);
    obj->type = type;
    obj->is_marked = false;
    obj->in_remembered_set = false;
    obj->generation = 0;
    obj->age = 0;
    obj->next = vm->objects;
    vm->objects = obj;
    return obj;
}

uint32_t ms_fnv1a(const char* data, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

static MsObjString* alloc_string(struct MsVM* vm, const char* chars, int length,
                                  uint32_t hash) {
    MsObjString* s = MS_ALLOC_OBJ(vm, MS_OBJ_STRING, MsObjString,
                                   (size_t)length + 1);
    s->length = length;
    s->hash = hash;
    memcpy(s->data, chars, (size_t)length);
    s->data[length] = '\0';
    ms_table_set(&vm->strings, s, MS_BOOL_VAL(true));
    return s;
}

MsObjString* ms_obj_string_copy(struct MsVM* vm, const char* chars, int length) {
    uint32_t hash = ms_fnv1a(chars, length);
    MsObjString* interned = ms_table_find_string(&vm->strings, chars, length, hash);
    if (interned) return interned;
    return alloc_string(vm, chars, length, hash);
}

MsObjString* ms_obj_string_take(struct MsVM* vm, char* chars, int length) {
    uint32_t hash = ms_fnv1a(chars, length);
    MsObjString* interned = ms_table_find_string(&vm->strings, chars, length, hash);
    if (interned) {
        free(chars);
        return interned;
    }
    MsObjString* s = alloc_string(vm, chars, length, hash);
    free(chars);
    return s;
}

MsObjString* ms_obj_string_concat(struct MsVM* vm, MsObjString* a, MsObjString* b) {
    if (a->length > INT_MAX - b->length) abort();
    int length = a->length + b->length;
    char* buf = (char*)malloc((size_t)length + 1);
    if (!buf) abort();
    memcpy(buf, a->data, (size_t)a->length);
    memcpy(buf + a->length, b->data, (size_t)b->length);
    buf[length] = '\0';

    uint32_t hash = ms_fnv1a(buf, length);
    MsObjString* interned = ms_table_find_string(&vm->strings, buf, length, hash);
    if (interned) {
        free(buf);
        return interned;
    }
    MsObjString* s = alloc_string(vm, buf, length, hash);
    free(buf);
    return s;
}

void ms_object_print(MsObject* obj) {
    switch (obj->type) {
        case MS_OBJ_STRING:
            printf("%s", ((MsObjString*)obj)->data);
            break;
        default:
            printf("<object %d>", (int)obj->type);
            break;
    }
}

void ms_object_free(struct MsVM* vm, MsObject* obj) {
    switch (obj->type) {
        case MS_OBJ_STRING: {
            MsObjString* s = (MsObjString*)obj;
            ms_reallocate(vm, obj, sizeof(MsObjString) + (size_t)s->length + 1, 0);
            break;
        }
        default:
            ms_reallocate(vm, obj, 0, 0);
            break;
    }
}
