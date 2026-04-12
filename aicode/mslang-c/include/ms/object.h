#pragma once
#include "ms/common.h"
#include "ms/value.h"

typedef enum {
    MS_OBJ_STRING,
    MS_OBJ_FUNCTION,
    MS_OBJ_NATIVE,
    MS_OBJ_CLOSURE,
    MS_OBJ_UPVALUE,
    MS_OBJ_CLASS,
    MS_OBJ_INSTANCE,
    MS_OBJ_BOUND_METHOD,
    MS_OBJ_LIST,
    MS_OBJ_MAP,
    MS_OBJ_MODULE,
    MS_OBJ_STRING_BUILDER,
    MS_OBJ_TUPLE,
    MS_OBJ_FILE,
    MS_OBJ_WEAK_REF,
    MS_OBJ_COROUTINE,
} MsObjectType;

struct MsObject {
    MsObjectType type;
    bool is_marked;
    bool in_remembered_set;
    ms_u8 generation;
    ms_u8 age;
    struct MsObject* next;
};

typedef struct MsObjString {
    MsObject obj;
    uint32_t hash;
    int length;
    char data[];
} MsObjString;

#define MS_OBJ_TYPE(v)       (MS_AS_OBJECT(v)->type)
#define MS_IS_OBJ_TYPE(v, t) (MS_IS_OBJECT(v) && MS_OBJ_TYPE(v) == (t))
#define MS_IS_STRING(v)      MS_IS_OBJ_TYPE(v, MS_OBJ_STRING)
#define MS_AS_STRING(v)      ((MsObjString*)MS_AS_OBJECT(v))
#define MS_AS_CSTRING(v)     (MS_AS_STRING(v)->data)

struct MsVM;

MsObject* ms_alloc_object(struct MsVM* vm, size_t size, MsObjectType type);

#define MS_ALLOC_OBJ(vm, type_enum, T, extra) \
    (T*)ms_alloc_object((vm), sizeof(T) + (extra), (type_enum))

uint32_t     ms_fnv1a(const char* data, int length);
MsObjString* ms_obj_string_copy(struct MsVM* vm, const char* chars, int length);
MsObjString* ms_obj_string_take(struct MsVM* vm, char* chars, int length);
MsObjString* ms_obj_string_concat(struct MsVM* vm, MsObjString* a, MsObjString* b);

void ms_object_print(MsObject* obj);
void ms_object_free(struct MsVM* vm, MsObject* obj);
