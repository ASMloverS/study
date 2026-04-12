#include "ms/value.h"

void ms_value_array_init(MsValueArray* arr) {
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void ms_value_array_push(MsValueArray* arr, MsValue val) {
    MS_ARRAY_PUSH(arr, val, MsValue);
}

void ms_value_array_free(MsValueArray* arr) {
    free(arr->data);
    ms_value_array_init(arr);
}

bool ms_value_equals(MsValue a, MsValue b) {
    if (MS_IS_NUMERIC(a) && MS_IS_NUMERIC(b)) {
        return ms_as_double(a) == ms_as_double(b);
    }
    if (a.type != b.type) return false;
    switch (a.type) {
        case MS_VAL_NIL:    return true;
        case MS_VAL_BOOL:   return MS_AS_BOOL(a) == MS_AS_BOOL(b);
        case MS_VAL_OBJECT: return MS_AS_OBJECT(a) == MS_AS_OBJECT(b);
        default:            return false;
    }
}

bool ms_value_is_truthy(MsValue v) {
    switch (v.type) {
        case MS_VAL_NIL:    return false;
        case MS_VAL_BOOL:   return MS_AS_BOOL(v);
        case MS_VAL_INT:    return MS_AS_INT(v) != 0;
        case MS_VAL_NUMBER: return MS_AS_NUMBER(v) != 0.0;
        case MS_VAL_OBJECT: return true;
    }
    return false;
}

void ms_value_print(MsValue v) {
    char* s = ms_value_to_cstring(v);
    fputs(s, stdout);
    free(s);
}

char* ms_value_to_cstring(MsValue v) {
    char buf[64] = {0};
    switch (v.type) {
        case MS_VAL_NIL:    snprintf(buf, sizeof(buf), "nil"); break;
        case MS_VAL_BOOL:   snprintf(buf, sizeof(buf), "%s", MS_AS_BOOL(v) ? "true" : "false"); break;
        case MS_VAL_INT:    snprintf(buf, sizeof(buf), "%lld", (long long)MS_AS_INT(v)); break;
        case MS_VAL_NUMBER: snprintf(buf, sizeof(buf), "%g", MS_AS_NUMBER(v)); break;
        case MS_VAL_OBJECT: snprintf(buf, sizeof(buf), "<object>"); break;
    }
    return ms_strdup(buf);
}
