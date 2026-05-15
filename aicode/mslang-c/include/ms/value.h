#pragma once
#include "ms/common.h"

#if MS_NAN_BOXING

/* ---- NaN-boxing path ---- */
typedef struct MsObject MsObject;
#include "ms/value_nan.h"

#else

/* ---- Tagged-union path (default / MSVC fallback) ---- */

typedef enum {
    MS_VAL_NIL,
    MS_VAL_BOOL,
    MS_VAL_NUMBER,
    MS_VAL_INT,
    MS_VAL_OBJECT,
} MsValueType;

typedef struct MsObject MsObject;

typedef struct {
    MsValueType type;
    union {
        bool boolean;
        double number;
        ms_i64 integer;
        MsObject* object;
    } as;
} MsValue;

#define MS_NIL_VAL()     ((MsValue){MS_VAL_NIL,    {.integer = 0}})
#define MS_BOOL_VAL(b)   ((MsValue){MS_VAL_BOOL,   {.boolean = (b)}})
#define MS_NUMBER_VAL(n) ((MsValue){MS_VAL_NUMBER,  {.number = (n)}})
#define MS_INT_VAL(i)    ((MsValue){MS_VAL_INT,     {.integer = (i)}})
#define MS_OBJ_VAL(p)    ((MsValue){MS_VAL_OBJECT,  {.object = (MsObject*)(p)}})

#define MS_IS_NIL(v)     ((v).type == MS_VAL_NIL)
#define MS_IS_BOOL(v)    ((v).type == MS_VAL_BOOL)
#define MS_IS_NUMBER(v)  ((v).type == MS_VAL_NUMBER)
#define MS_IS_INT(v)     ((v).type == MS_VAL_INT)
#define MS_IS_OBJECT(v)  ((v).type == MS_VAL_OBJECT)
MS_INLINE bool ms_is_numeric(MsValue v) {
    return v.type == MS_VAL_NUMBER || v.type == MS_VAL_INT;
}
#define MS_IS_NUMERIC(v) ms_is_numeric(v)

#define MS_AS_BOOL(v)    ((v).as.boolean)
#define MS_AS_NUMBER(v)  ((v).as.number)
#define MS_AS_INT(v)     ((v).as.integer)
#define MS_AS_OBJECT(v)  ((v).as.object)

MS_INLINE double ms_as_double(MsValue v) {
    return MS_IS_INT(v) ? (double)MS_AS_INT(v) : MS_AS_NUMBER(v);
}

#endif /* MS_NAN_BOXING */

/* ---- MsValueArray (shared, layout-independent) ---- */

typedef struct {
    MsValue* data;
    int count;
    int capacity;
} MsValueArray;

#define MS_ARRAY_PUSH(arr, item, T) do { \
    if ((arr)->count >= (arr)->capacity) { \
        int _cap = (arr)->capacity < 8 ? 8 : (arr)->capacity * 2; \
        T* _new = (T*)realloc((arr)->data, sizeof(T) * (size_t)_cap); \
        if (!_new) abort(); \
        (arr)->data = _new; \
        (arr)->capacity = _cap; \
    } \
    (arr)->data[(arr)->count++] = (item); \
} while (0)

void ms_value_array_init(MsValueArray* arr);
void ms_value_array_push(MsValueArray* arr, MsValue val);
void ms_value_array_free(MsValueArray* arr);

bool  ms_value_equals(MsValue a, MsValue b);
bool  ms_value_is_truthy(MsValue v);
void  ms_value_print(MsValue v);
char* ms_value_to_cstring(MsValue v);
