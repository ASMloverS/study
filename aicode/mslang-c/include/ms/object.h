#pragma once
#include "ms/common.h"
#include "ms/consts.h"
#include "ms/value.h"
#include "ms/chunk.h"

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

// Forward declarations
struct MsVM;
typedef struct MsObjUpvalue  MsObjUpvalue;
typedef struct MsObjClosure  MsObjClosure;
typedef struct MsInlineCache MsInlineCache;

typedef MsValue (*MsNativeFn)(struct MsVM* vm, int argc, MsValue* argv);

typedef struct {
    MsObject      obj;
    int           arity;
    int           min_arity;
    int           upvalue_count;
    int           max_stack_size;
    bool          is_generator;
    MsChunk       chunk;
    MsObjString*  name;
    MsInlineCache* ic;
    int           ic_count;
} MsObjFunction;

typedef struct {
    MsObject     obj;
    MsNativeFn   function;
    MsObjString* name;
    int          arity;
} MsObjNative;

struct MsObjUpvalue {
    MsObject       obj;
    MsValue*       location;
    MsValue        closed;
    MsObjUpvalue*  next;
};

struct MsObjClosure {
    MsObject      obj;
    MsObjFunction* function;
    int            upvalue_count;
    MsObjUpvalue*  upvalues[];
};

#define MS_IS_FUNCTION(v)  MS_IS_OBJ_TYPE(v, MS_OBJ_FUNCTION)
#define MS_AS_FUNCTION(v)  ((MsObjFunction*)MS_AS_OBJECT(v))
#define MS_IS_NATIVE(v)    MS_IS_OBJ_TYPE(v, MS_OBJ_NATIVE)
#define MS_AS_NATIVE(v)    ((MsObjNative*)MS_AS_OBJECT(v))
#define MS_IS_CLOSURE(v)   MS_IS_OBJ_TYPE(v, MS_OBJ_CLOSURE)
#define MS_AS_CLOSURE(v)   ((MsObjClosure*)MS_AS_OBJECT(v))
#define MS_IS_UPVALUE(v)   MS_IS_OBJ_TYPE(v, MS_OBJ_UPVALUE)

MsObject* ms_alloc_object(struct MsVM* vm, size_t size, MsObjectType type);

#define MS_ALLOC_OBJ(vm, type_enum, T, extra) \
    (T*)ms_alloc_object((vm), sizeof(T) + (extra), (type_enum))

uint32_t     ms_fnv1a(const char* data, int length);
MsObjString* ms_obj_string_copy(struct MsVM* vm, const char* chars, int length);
MsObjString* ms_obj_string_take(struct MsVM* vm, char* chars, int length);
MsObjString* ms_obj_string_concat(struct MsVM* vm, MsObjString* a, MsObjString* b);

void ms_object_print(MsObject* obj);
void ms_object_free(struct MsVM* vm, MsObject* obj);

MsObjFunction* ms_obj_function_new(struct MsVM* vm);
MsObjNative*   ms_obj_native_new(struct MsVM* vm, MsNativeFn fn,
                                  const char* name, int arity);
MsObjUpvalue*  ms_obj_upvalue_new(struct MsVM* vm, MsValue* slot);
MsObjClosure*  ms_obj_closure_new(struct MsVM* vm, MsObjFunction* fn);

/* ---- OOP: Class, Instance, BoundMethod ---- */
#include "ms/table_types.h"
#include "ms/shape.h"

typedef struct MsObjClass {
    MsObject obj;
    MsObjString* name;
    MsTable methods;
    MsTable* static_methods;
    MsTable* getters;
    MsTable* setters;
    MsTable* abstract_methods;
    struct MsObjClass* superclass;
    MsShape* root_shape; /* shared root shape for all instances of this class */
} MsObjClass;

typedef struct {
    MsObject      obj;
    MsObjClass*   klass;
    MsShape*      shape;                         /* current hidden class */
    MsValue       inline_fields[MS_SBO_FIELDS];  /* SBO: first 8 fields inline */
    MsValue*      overflow_fields;               /* NULL until > MS_SBO_FIELDS fields */
    int           field_count;
} MsObjInstance;

typedef struct {
    MsObject obj;
    MsValue receiver;
    MsObjClosure* method;
} MsObjBoundMethod;

#define MS_IS_CLASS(v)         MS_IS_OBJ_TYPE(v, MS_OBJ_CLASS)
#define MS_AS_CLASS(v)         ((MsObjClass*)MS_AS_OBJECT(v))
#define MS_IS_INSTANCE(v)      MS_IS_OBJ_TYPE(v, MS_OBJ_INSTANCE)
#define MS_AS_INSTANCE(v)      ((MsObjInstance*)MS_AS_OBJECT(v))
#define MS_IS_BOUND_METHOD(v)  MS_IS_OBJ_TYPE(v, MS_OBJ_BOUND_METHOD)
#define MS_AS_BOUND_METHOD(v)  ((MsObjBoundMethod*)MS_AS_OBJECT(v))

MsObjClass*       ms_obj_class_new(struct MsVM* vm, MsObjString* name);
MsObjInstance*    ms_obj_instance_new(struct MsVM* vm, MsObjClass* klass);
MsObjBoundMethod* ms_obj_bound_method_new(struct MsVM* vm, MsValue receiver,
                                           MsObjClosure* method);

/* ---- Collections: List, Map, Tuple ---- */
#include "ms/vtable.h"

typedef struct {
    MsObject obj;
    MsValueArray items;
} MsObjList;

typedef struct {
    MsObject obj;
    MsValueTable table;
} MsObjMap;

typedef struct {
    MsObject obj;
    uint32_t hash;
    int count;
    MsValue items[];  /* flexible array member */
} MsObjTuple;

#define MS_IS_LIST(v)   MS_IS_OBJ_TYPE(v, MS_OBJ_LIST)
#define MS_AS_LIST(v)   ((MsObjList*)MS_AS_OBJECT(v))
#define MS_IS_MAP(v)    MS_IS_OBJ_TYPE(v, MS_OBJ_MAP)
#define MS_AS_MAP(v)    ((MsObjMap*)MS_AS_OBJECT(v))
#define MS_IS_TUPLE(v)  MS_IS_OBJ_TYPE(v, MS_OBJ_TUPLE)
#define MS_AS_TUPLE(v)  ((MsObjTuple*)MS_AS_OBJECT(v))

MsObjList*  ms_obj_list_new(struct MsVM* vm);
MsObjMap*   ms_obj_map_new(struct MsVM* vm);
MsObjTuple* ms_obj_tuple_new(struct MsVM* vm, MsValue* items, int count);
