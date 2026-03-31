#include "value.h"
#include "memory.h"
#include <stdbool.h>
#include <stdio.h>

bool ms_values_equal(MsValue a, MsValue b)
{
	if (a.type != b.type)
		return false;
	switch (a.type) {
	case MS_VAL_NIL:    return true;
	case MS_VAL_BOOL:   return a.boolean == b.boolean;
	case MS_VAL_NUMBER: return a.number == b.number;
	case MS_VAL_OBJ:    return a.obj == b.obj;
	}
	return false;
}

bool ms_is_falsey(MsValue value)
{
	return ms_is_nil(value) || (ms_is_bool(value) && !ms_as_bool(value));
}

void ms_print_value(MsValue value)
{
	switch (value.type) {
	case MS_VAL_NIL:    printf("nil"); break;
	case MS_VAL_BOOL:   printf(value.boolean ? "true" : "false"); break;
	case MS_VAL_NUMBER: printf("%.14g", value.number); break;
	case MS_VAL_OBJ:    printf("<obj>"); break;
	}
}

void ms_value_array_init(MsValueArray *array)
{
	array->values = NULL;
	array->count = 0;
	array->capacity = 0;
}

void ms_value_array_free(MsValueArray *array)
{
	MS_FREE_ARRAY(MsValue, array->values, array->capacity);
	ms_value_array_init(array);
}

void ms_value_array_write(MsValueArray *array, MsValue value)
{
	if (array->capacity < array->count + 1) {
		int old_capacity = array->capacity;
		array->capacity = MS_GROW_CAPACITY(old_capacity);
		array->values = MS_GROW_ARRAY(MsValue, array->values,
					      old_capacity, array->capacity);
	}
	array->values[array->count] = value;
	array->count++;
}
