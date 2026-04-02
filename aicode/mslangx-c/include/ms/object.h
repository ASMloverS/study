#ifndef MSLANGC_OBJECT_H_
#define MSLANGC_OBJECT_H_

#include <stddef.h>

#include "ms/value.h"

typedef struct MsTable MsTable;

typedef enum MsObjectType {
  MS_OBJ_STRING,
  MS_OBJ_FUNCTION,
  MS_OBJ_CLOSURE,
  MS_OBJ_UPVALUE,
  MS_OBJ_CLASS,
  MS_OBJ_INSTANCE,
  MS_OBJ_BOUND_METHOD,
  MS_OBJ_NATIVE_FN,
  MS_OBJ_LIST,
  MS_OBJ_TUPLE,
  MS_OBJ_MAP,
  MS_OBJ_MODULE
} MsObjectType;

typedef struct MsObject {
  MsObjectType type;
  unsigned char marked;
  struct MsObject *next;
} MsObject;

typedef struct MsValueArray {
  size_t count;
  size_t capacity;
  MsValue *items;
} MsValueArray;

typedef struct MsList {
  MsObject object;
  MsValueArray elements;
} MsList;

typedef struct MsTuple {
  MsObject object;
  MsValueArray elements;
} MsTuple;

typedef struct MsMap {
  MsObject object;
  MsTable *entries;
} MsMap;

void ms_object_init(MsObject *object, MsObjectType type);
const char *ms_object_type_name(MsObjectType type);
void ms_value_array_init(MsValueArray *array);
void ms_value_array_destroy(MsValueArray *array);
int ms_value_array_reserve(MsValueArray *array, size_t min_capacity);
int ms_value_array_append(MsValueArray *array, MsValue value);
MsList *ms_list_new(void);
void ms_list_free(MsList *list);
MsTuple *ms_tuple_new(void);
void ms_tuple_free(MsTuple *tuple);
MsMap *ms_map_new(void);
void ms_map_free(MsMap *map);

#endif
