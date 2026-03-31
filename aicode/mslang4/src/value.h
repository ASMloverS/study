#ifndef MS_VALUE_H
#define MS_VALUE_H

#include "common.h"

typedef struct MsObject MsObject;

typedef enum { MS_VAL_NIL, MS_VAL_BOOL, MS_VAL_NUMBER, MS_VAL_OBJ } MsValueType;

typedef struct {
	MsValueType type;
	union {
		bool boolean;
		double number;
		MsObject *obj;
	};
} MsValue;

static inline MsValue ms_nil_val(void)
{
	return (MsValue){ .type = MS_VAL_NIL, .number = 0 };
}

static inline MsValue ms_bool_val(bool v)
{
	return (MsValue){ .type = MS_VAL_BOOL, .boolean = v };
}

static inline MsValue ms_number_val(double v)
{
	return (MsValue){ .type = MS_VAL_NUMBER, .number = v };
}

static inline MsValue ms_obj_val(MsObject *o)
{
	return (MsValue){ .type = MS_VAL_OBJ, .obj = o };
}

static inline bool ms_is_nil(MsValue v)    { return v.type == MS_VAL_NIL; }
static inline bool ms_is_bool(MsValue v)   { return v.type == MS_VAL_BOOL; }
static inline bool ms_is_number(MsValue v) { return v.type == MS_VAL_NUMBER; }
static inline bool ms_is_obj(MsValue v)    { return v.type == MS_VAL_OBJ; }

static inline bool ms_as_bool(MsValue v)     { return v.boolean; }
static inline double ms_as_number(MsValue v) { return v.number; }
static inline MsObject *ms_as_obj(MsValue v) { return v.obj; }

bool ms_values_equal(MsValue a, MsValue b);
bool ms_is_falsey(MsValue value);
void ms_print_value(MsValue value);

typedef struct {
	MsValue *values;
	int count;
	int capacity;
} MsValueArray;

void ms_value_array_init(MsValueArray *array);
void ms_value_array_free(MsValueArray *array);
void ms_value_array_write(MsValueArray *array, MsValue value);

#endif
