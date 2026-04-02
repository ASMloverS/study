#include "ms/object.h"

#include <stddef.h>
#include <stdlib.h>

#include "ms/table.h"

void ms_object_init(MsObject *object, MsObjectType type) {
  if (object == NULL) {
    return;
  }

  object->type = type;
  object->marked = 0;
  object->next = NULL;
}

const char *ms_object_type_name(MsObjectType type) {
  switch (type) {
    case MS_OBJ_STRING:
      return "string";
    case MS_OBJ_FUNCTION:
      return "function";
    case MS_OBJ_CLOSURE:
      return "closure";
    case MS_OBJ_UPVALUE:
      return "upvalue";
    case MS_OBJ_CLASS:
      return "class";
    case MS_OBJ_INSTANCE:
      return "instance";
    case MS_OBJ_BOUND_METHOD:
      return "bound_method";
    case MS_OBJ_NATIVE_FN:
      return "native_function";
    case MS_OBJ_LIST:
      return "list";
    case MS_OBJ_TUPLE:
      return "tuple";
    case MS_OBJ_MAP:
      return "map";
    case MS_OBJ_MODULE:
      return "module";
  }

  return "object";
}

void ms_value_array_init(MsValueArray *array) {
  if (array == NULL) {
    return;
  }

  array->count = 0;
  array->capacity = 0;
  array->items = NULL;
}

void ms_value_array_destroy(MsValueArray *array) {
  if (array == NULL) {
    return;
  }

  free(array->items);
  ms_value_array_init(array);
}

int ms_value_array_reserve(MsValueArray *array, size_t min_capacity) {
  MsValue *items;
  size_t new_capacity;

  if (array == NULL) {
    return 0;
  }
  if (min_capacity <= array->capacity) {
    return 1;
  }

  new_capacity = array->capacity == 0 ? 4 : array->capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  items = (MsValue *) realloc(array->items, new_capacity * sizeof(*items));
  if (items == NULL) {
    return 0;
  }

  array->items = items;
  array->capacity = new_capacity;
  return 1;
}

int ms_value_array_append(MsValueArray *array, MsValue value) {
  if (array == NULL || !ms_value_array_reserve(array, array->count + 1)) {
    return 0;
  }

  array->items[array->count] = value;
  array->count += 1;
  return 1;
}

MsList *ms_list_new(void) {
  MsList *list = NULL;

  list = (MsList *) calloc(1, sizeof(*list));
  if (list == NULL) {
    return NULL;
  }

  ms_object_init(&list->object, MS_OBJ_LIST);
  ms_value_array_init(&list->elements);
  return list;
}

void ms_list_free(MsList *list) {
  if (list == NULL) {
    return;
  }

  ms_value_array_destroy(&list->elements);
  free(list);
}

MsTuple *ms_tuple_new(void) {
  MsTuple *tuple = NULL;

  tuple = (MsTuple *) calloc(1, sizeof(*tuple));
  if (tuple == NULL) {
    return NULL;
  }

  ms_object_init(&tuple->object, MS_OBJ_TUPLE);
  ms_value_array_init(&tuple->elements);
  return tuple;
}

void ms_tuple_free(MsTuple *tuple) {
  if (tuple == NULL) {
    return;
  }

  ms_value_array_destroy(&tuple->elements);
  free(tuple);
}

MsMap *ms_map_new(void) {
  MsMap *map = NULL;

  map = (MsMap *) calloc(1, sizeof(*map));
  if (map == NULL) {
    return NULL;
  }

  ms_object_init(&map->object, MS_OBJ_MAP);
  map->entries = (MsTable *) calloc(1, sizeof(*map->entries));
  if (map->entries == NULL) {
    free(map);
    return NULL;
  }

  ms_table_init(map->entries);
  return map;
}

void ms_map_free(MsMap *map) {
  if (map == NULL) {
    return;
  }

  if (map->entries != NULL) {
    ms_table_destroy(map->entries);
  }
  free(map->entries);
  free(map);
}
