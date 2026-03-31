#ifndef MS_OBJECT_H
#define MS_OBJECT_H

#include "value.h"

typedef enum {
	MS_OBJ_STRING,
	MS_OBJ_FUNCTION,
	MS_OBJ_CLOSURE,
	MS_OBJ_UPVALUE,
	MS_OBJ_CLASS,
	MS_OBJ_INSTANCE,
	MS_OBJ_BOUND_METHOD,
	MS_OBJ_MODULE,
	MS_OBJ_LIST,
	MS_OBJ_NATIVE
} MsObjectType;

struct MsObject {
	MsObjectType type;
	MsObject *next;
	bool isMarked;
};

typedef struct {
	MsObject base;
	char *chars;
	int length;
	uint32_t hash;
} MsString;

typedef struct MsVM MsVM;
typedef MsValue (*MsNativeFn)(MsVM *vm, int argCount, MsValue *args);
typedef struct {
	MsObject base;
	MsNativeFn function;
	MsString *name;
	int arity;
} MsNative;
typedef struct {
	MsObject base;
	int arity;
	int upvalueCount;
	char *_chunk_placeholder;
	MsString *name;
} MsFunction;
typedef struct MsUpvalue {
	MsObject base;
	MsValue *location;
	MsValue closed;
	struct MsUpvalue *next;
} MsUpvalue;
typedef struct {
	MsObject base;
	MsFunction *function;
	MsUpvalue **upvalues;
	int upvalueCount;
} MsClosure;
typedef struct MsClass MsClass;
typedef struct {
	MsObject base;
	MsClass *klass;
	int _fields_placeholder;
} MsInstance;
typedef struct {
	MsObject base;
	MsValue receiver;
	MsClosure *method;
} MsBoundMethod;
typedef struct {
	MsObject base;
	MsString *name;
	char *path;
	int _exports_placeholder;
	bool isLoaded;
} MsModule;
typedef struct {
	MsObject base;
	MsValue *elements;
	int count;
	int capacity;
} MsList;

#define MS_IS_STRING(v) (ms_is_obj(v) && ms_as_obj(v)->type == MS_OBJ_STRING)
#define MS_AS_STRING(v) ((MsString *)ms_as_obj(v))

MsString *ms_string_copy(const char *chars, int length);
MsString *ms_string_take(char *chars, int length);
uint32_t ms_string_hash(const char *key, int length);
MsString *ms_string_concat(MsString *a, MsString *b);
void ms_object_free(MsObject *obj);
void ms_object_print(MsValue value);

#endif
