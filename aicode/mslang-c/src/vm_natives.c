#include "ms/vm.h"
#include "ms/value.h"
#include "ms/object.h"
#include <stdio.h>
#include <stdlib.h>

static MsValue native_print(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        if (MS_IS_OBJECT(argv[i]))
            ms_object_print(MS_AS_OBJECT(argv[i]));
        else
            ms_value_print(argv[i]);
    }
    printf("\n");
    return MS_NIL_VAL();
}

static MsValue native_type(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_OBJ_VAL(ms_obj_string_copy(vm, "nil", 3));
    const char* t = "nil";
    switch (argv[0].type) {
    case MS_VAL_BOOL:   t = "bool";    break;
    case MS_VAL_NUMBER: t = "number";  break;
    case MS_VAL_INT:    t = "int";     break;
    case MS_VAL_OBJECT:
        switch (MS_OBJ_TYPE(argv[0])) {
        case MS_OBJ_STRING:   t = "string";   break;
        case MS_OBJ_FUNCTION: t = "function"; break;
        case MS_OBJ_CLOSURE:  t = "function"; break;
        case MS_OBJ_NATIVE:   t = "native";   break;
        default:              t = "object";   break;
        }
        break;
    default: break;
    }
    return MS_OBJ_VAL(ms_obj_string_copy(vm, t, (int)strlen(t)));
}

static MsValue native_tostring(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) return MS_OBJ_VAL(ms_obj_string_copy(vm, "nil", 3));
    char* s = ms_value_to_cstring(argv[0]);
    MsObjString* os = ms_obj_string_copy(vm, s, (int)strlen(s));
    free(s);
    return MS_OBJ_VAL(os);
}

static MsValue native_toint(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_INT_VAL(0);
    if (MS_IS_INT(argv[0]))    return argv[0];
    if (MS_IS_NUMBER(argv[0])) return MS_INT_VAL((ms_i64)MS_AS_NUMBER(argv[0]));
    return MS_INT_VAL(0);
}

static MsValue native_tofloat(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_NUMBER_VAL(0.0);
    if (MS_IS_NUMBER(argv[0])) return argv[0];
    if (MS_IS_INT(argv[0]))    return MS_NUMBER_VAL((double)MS_AS_INT(argv[0]));
    return MS_NUMBER_VAL(0.0);
}

static MsValue native_resume(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_COROUTINE(argv[0])) {
        ms_vm_runtime_error(vm, "resume: expected coroutine as first argument.");
        return MS_NIL_VAL();
    }
    MsObjCoroutine* co = MS_AS_COROUTINE(argv[0]);
    MsValue sent = (argc >= 2) ? argv[1] : MS_NIL_VAL();
    MsValue result = MS_NIL_VAL();
    ms_vm_coro_resume(vm, co, sent, &result);
    return result;
}

void ms_vm_register_natives(MsVM* vm) {
    ms_vm_define_native(vm, "print",    native_print,    -1);
    ms_vm_define_native(vm, "type",     native_type,      1);
    ms_vm_define_native(vm, "tostring", native_tostring,  1);
    ms_vm_define_native(vm, "toint",    native_toint,     1);
    ms_vm_define_native(vm, "tofloat",  native_tofloat,   1);
    ms_vm_define_native(vm, "resume",   native_resume,   -1);
}
