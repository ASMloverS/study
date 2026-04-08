#ifndef MSLANGC_VALUE_H_
#define MSLANGC_VALUE_H_

#include <stddef.h>

typedef enum MsValueType {
  MS_VAL_NIL,
  MS_VAL_BOOL,
  MS_VAL_NUMBER,
  MS_VAL_OBJECT
} MsValueType;

typedef struct MsObject MsObject;
typedef struct MsString MsString;
typedef struct MsFunction MsFunction;
typedef struct MsClosure MsClosure;
typedef struct MsUpvalue MsUpvalue;
typedef struct MsClass MsClass;
typedef struct MsInstance MsInstance;
typedef struct MsBoundMethod MsBoundMethod;
typedef struct MsNativeFunction MsNativeFunction;
typedef struct MsList MsList;
typedef struct MsTuple MsTuple;
typedef struct MsMap MsMap;
typedef struct MsModule MsModule;

typedef struct MsValue {
  MsValueType type;
  union {
    int boolean;
    double number;
    MsObject* object;
  } as;
} MsValue;

MsValue ms_value_nil(void);
MsValue ms_value_bool(int boolean);
MsValue ms_value_number(double number);
MsValue ms_value_object(MsObject* object);

int ms_value_is_nil(MsValue value);
int ms_value_is_bool(MsValue value);
int ms_value_is_number(MsValue value);
int ms_value_is_object(MsValue value);
int ms_value_is_string(MsValue value);
int ms_value_is_function(MsValue value);
int ms_value_is_closure(MsValue value);
int ms_value_is_upvalue(MsValue value);
int ms_value_is_class(MsValue value);
int ms_value_is_instance(MsValue value);
int ms_value_is_bound_method(MsValue value);
int ms_value_is_native_function(MsValue value);
int ms_value_is_list(MsValue value);
int ms_value_is_tuple(MsValue value);
int ms_value_is_map(MsValue value);
int ms_value_is_module(MsValue value);

int ms_value_get_bool(MsValue value, int* out_boolean);
int ms_value_get_number(MsValue value, double* out_number);
int ms_value_get_object(MsValue value, MsObject** out_object);
int ms_value_get_string(MsValue value, MsString** out_string);
int ms_value_get_function(MsValue value, MsFunction** out_function);
int ms_value_get_closure(MsValue value, MsClosure** out_closure);
int ms_value_get_upvalue(MsValue value, MsUpvalue** out_upvalue);
int ms_value_get_class(MsValue value, MsClass** out_class);
int ms_value_get_instance(MsValue value, MsInstance** out_instance);
int ms_value_get_bound_method(MsValue value, MsBoundMethod** out_bound_method);
int ms_value_get_native_function(MsValue value,
                                 MsNativeFunction** out_function);
int ms_value_get_list(MsValue value, MsList** out_list);
int ms_value_get_tuple(MsValue value, MsTuple** out_tuple);
int ms_value_get_map(MsValue value, MsMap** out_map);
int ms_value_get_module(MsValue value, MsModule** out_module);

int ms_value_length(MsValue value, int* out_length);
int ms_value_is_falsey(MsValue value);
int ms_value_equals(MsValue left, MsValue right);
int ms_value_format(MsValue value, char* buffer, size_t buffer_size);

#endif
