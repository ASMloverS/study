#include "ms/value.h"
#include "ms/object.h"

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
#if MS_NAN_BOXING
    /* Fast-path: same bit pattern covers nil==nil, bool==bool, int==int,
       obj==obj (interned strings), and matching doubles. */
    if (a == b) return true;
    /* Cross-type numeric: int vs double */
    if (MS_IS_NUMERIC(a) && MS_IS_NUMERIC(b))
        return ms_as_double(a) == ms_as_double(b);
    return false;
#else
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
#endif
}

bool ms_value_is_truthy(MsValue v) {
#if MS_NAN_BOXING
    if (MS_IS_NIL(v))    return false;
    if (MS_IS_BOOL(v))   return MS_AS_BOOL(v);
    if (MS_IS_INT(v))    return MS_AS_INT(v) != 0;
    if (MS_IS_DOUBLE(v)) return MS_AS_NUMBER(v) != 0.0;
    return true; /* object */
#else
    switch (v.type) {
        case MS_VAL_NIL:    return false;
        case MS_VAL_BOOL:   return MS_AS_BOOL(v);
        case MS_VAL_INT:    return MS_AS_INT(v) != 0;
        case MS_VAL_NUMBER: return MS_AS_NUMBER(v) != 0.0;
        case MS_VAL_OBJECT: return true;
    }
    return false;
#endif
}

void ms_value_print(MsValue v) {
    char* s = ms_value_to_cstring(v);
    fputs(s, stdout);
    free(s);
}

char* ms_value_to_cstring(MsValue v) {
    char buf[64] = {0};
#if MS_NAN_BOXING
    if (MS_IS_NIL(v))    { snprintf(buf, sizeof(buf), "nil"); }
    else if (MS_IS_BOOL(v))   { snprintf(buf, sizeof(buf), "%s", MS_AS_BOOL(v) ? "true" : "false"); }
    else if (MS_IS_INT(v))    { snprintf(buf, sizeof(buf), "%lld", (long long)MS_AS_INT(v)); }
    else if (MS_IS_DOUBLE(v)) { snprintf(buf, sizeof(buf), "%g", MS_AS_NUMBER(v)); }
    else {
        /* object */
        if (MS_IS_STRING(v)) return ms_strdup(MS_AS_CSTRING(v));
        snprintf(buf, sizeof(buf), "<object>");
    }
#else
    switch (v.type) {
        case MS_VAL_NIL:    snprintf(buf, sizeof(buf), "nil"); break;
        case MS_VAL_BOOL:   snprintf(buf, sizeof(buf), "%s", MS_AS_BOOL(v) ? "true" : "false"); break;
        case MS_VAL_INT:    snprintf(buf, sizeof(buf), "%lld", (long long)MS_AS_INT(v)); break;
        case MS_VAL_NUMBER: snprintf(buf, sizeof(buf), "%g", MS_AS_NUMBER(v)); break;
        case MS_VAL_OBJECT:
            if (MS_IS_STRING(v)) return ms_strdup(MS_AS_CSTRING(v));
            snprintf(buf, sizeof(buf), "<object>");
            break;
    }
#endif
    return ms_strdup(buf);
}
