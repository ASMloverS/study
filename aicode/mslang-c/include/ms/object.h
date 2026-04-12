#pragma once
#include "ms/common.h"

typedef enum {
    MS_OBJ_STRING,
    MS_OBJ_FUNCTION,
    MS_OBJ_CLOSURE,
    MS_OBJ_CLASS,
    MS_OBJ_INSTANCE,
    MS_OBJ_LIST,
    MS_OBJ_MAP,
    MS_OBJ_TUPLE,
    MS_OBJ_COROUTINE,
    MS_OBJ_NATIVE,
    MS_OBJ_UPVALUE,
    MS_OBJ_MODULE,
} MsObjType;

typedef struct MsObject {
    MsObjType type;
    bool is_marked;
    struct MsObject* next;
} MsObject;

typedef struct MsObjString {
    MsObject obj;
    uint32_t hash;
    int length;
    char* chars;
} MsObjString;
