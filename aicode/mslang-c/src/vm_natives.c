#include "ms/vm.h"
#include "ms/value.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/shape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static MsValue native_clock(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm); MS_UNUSED(argc); MS_UNUSED(argv);
    return MS_NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

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
    MS_UNUSED(argc);
    if (argc < 1) return MS_OBJ_VAL(ms_obj_string_copy(vm, "nil", 3));
    const char* t = "nil";
    MsValue val = argv[0];
    switch (val.type) {
    case MS_VAL_BOOL:   t = "bool";   break;
    case MS_VAL_NUMBER: t = "number"; break;
    case MS_VAL_INT:    t = "int";    break;
    case MS_VAL_OBJECT:
        switch (MS_OBJ_TYPE(val)) {
        case MS_OBJ_STRING:   t = "string";   break;
        case MS_OBJ_FUNCTION:
        case MS_OBJ_CLOSURE:
        case MS_OBJ_NATIVE:   t = "function"; break;
        case MS_OBJ_CLASS:    t = "class";    break;
        case MS_OBJ_INSTANCE: t = "instance"; break;
        case MS_OBJ_LIST:     t = "list";     break;
        case MS_OBJ_MAP:      t = "map";      break;
        case MS_OBJ_TUPLE:    t = "tuple";    break;
        default:              t = "object";   break;
        }
        break;
    default: break;
    }
    return MS_OBJ_VAL(ms_obj_string_copy(vm, t, (int)strlen(t)));
}

static MsValue native_str(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) return MS_OBJ_VAL(ms_obj_string_copy(vm, "nil", 3));
    if (MS_IS_STRING(argv[0])) return argv[0];
    char* s = ms_value_to_cstring(argv[0]);
    MsObjString* os = ms_obj_string_copy(vm, s, (int)strlen(s));
    free(s);
    return MS_OBJ_VAL(os);
}

static MsValue native_num(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_NUMBER_VAL(0.0);
    if (MS_IS_NUMBER(argv[0])) return argv[0];
    if (MS_IS_INT(argv[0]))    return MS_NUMBER_VAL((double)MS_AS_INT(argv[0]));
    if (MS_IS_STRING(argv[0])) {
        double d = strtod(MS_AS_CSTRING(argv[0]), NULL);
        return MS_NUMBER_VAL(d);
    }
    return MS_NUMBER_VAL(0.0);
}

static MsValue native_int(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_INT_VAL(0);
    if (MS_IS_INT(argv[0]))    return argv[0];
    if (MS_IS_NUMBER(argv[0])) return MS_INT_VAL((ms_i64)MS_AS_NUMBER(argv[0]));
    if (MS_IS_STRING(argv[0])) {
        long long v = strtoll(MS_AS_CSTRING(argv[0]), NULL, 10);
        return MS_INT_VAL((ms_i64)v);
    }
    return MS_INT_VAL(0);
}

static MsValue native_float(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_NUMBER_VAL(0.0);
    if (MS_IS_NUMBER(argv[0])) return argv[0];
    if (MS_IS_INT(argv[0]))    return MS_NUMBER_VAL((double)MS_AS_INT(argv[0]));
    if (MS_IS_STRING(argv[0])) {
        double d = strtod(MS_AS_CSTRING(argv[0]), NULL);
        return MS_NUMBER_VAL(d);
    }
    return MS_NUMBER_VAL(0.0);
}

static MsValue native_len(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) { ms_vm_runtime_error(vm, "len() requires 1 argument."); return MS_NIL_VAL(); }
    MsValue val = argv[0];
    if (MS_IS_STRING(val)) return MS_INT_VAL(MS_AS_STRING(val)->length);
    if (MS_IS_LIST(val))   return MS_INT_VAL(MS_AS_LIST(val)->items.count);
    if (MS_IS_MAP(val))    return MS_INT_VAL(MS_AS_MAP(val)->table.count);
    if (MS_IS_TUPLE(val))  return MS_INT_VAL(MS_AS_TUPLE(val)->count);
    ms_vm_runtime_error(vm, "len() not supported for this type.");
    return MS_NIL_VAL();
}

static MsValue native_input(MsVM* vm, int argc, MsValue* argv) {
    if (argc >= 1 && MS_IS_STRING(argv[0]))
        printf("%s", MS_AS_CSTRING(argv[0]));
    char buf[1024];
    if (!fgets(buf, (int)sizeof(buf), stdin))
        return MS_OBJ_VAL(ms_obj_string_copy(vm, "", 0));
    int len = (int)strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') { buf[len - 1] = '\0'; len--; }
    return MS_OBJ_VAL(ms_obj_string_copy(vm, buf, len));
}

static MsValue native_assert(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) { ms_vm_runtime_error(vm, "assert() requires at least 1 argument."); return MS_NIL_VAL(); }
    MsValue cond = argv[0];
    bool ok = !MS_IS_NIL(cond) && !(MS_IS_BOOL(cond) && !MS_AS_BOOL(cond));
    if (!ok) {
        if (argc >= 2 && MS_IS_STRING(argv[1]))
            ms_vm_runtime_error(vm, "Assertion failed: %s", MS_AS_CSTRING(argv[1]));
        else
            ms_vm_runtime_error(vm, "Assertion failed.");
    }
    return MS_NIL_VAL();
}

static MsValue native_hasattr(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 2 || !MS_IS_INSTANCE(argv[0]) || !MS_IS_STRING(argv[1]))
        return MS_BOOL_VAL(false);
    MsObjInstance* inst = MS_AS_INSTANCE(argv[0]);
    MsObjString*   name = MS_AS_STRING(argv[1]);
    int slot = ms_shape_find_slot(inst->shape, name);
    if (slot >= 0) return MS_BOOL_VAL(true);
    MsValue dummy;
    if (ms_table_get(&inst->klass->methods, name, &dummy)) return MS_BOOL_VAL(true);
    return MS_BOOL_VAL(false);
}

static MsValue native_getattr(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_INSTANCE(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "getattr() requires instance and string name.");
        return MS_NIL_VAL();
    }
    MsObjInstance* inst = MS_AS_INSTANCE(argv[0]);
    MsObjString*   name = MS_AS_STRING(argv[1]);
    int slot = ms_shape_find_slot(inst->shape, name);
    if (slot >= 0)
        return *ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot);
    MsValue method;
    if (ms_table_get(&inst->klass->methods, name, &method)) return method;
    ms_vm_runtime_error(vm, "Attribute '%s' not found.", name->data);
    return MS_NIL_VAL();
}

static MsValue native_setattr(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 3 || !MS_IS_INSTANCE(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "setattr() requires instance, string name, and value.");
        return MS_NIL_VAL();
    }
    MsObjInstance* inst = MS_AS_INSTANCE(argv[0]);
    MsObjString*   name = MS_AS_STRING(argv[1]);
    MsValue        val  = argv[2];
    int slot = ms_shape_find_slot(inst->shape, name);
    if (slot < 0) {
        inst->shape = ms_shape_transition(vm, inst->shape, name);
        slot = ms_shape_find_slot(inst->shape, name);
    }
    if (slot >= MS_SBO_FIELDS && !inst->overflow_fields) {
        int extra = slot - MS_SBO_FIELDS + 1;
        inst->overflow_fields = (MsValue*)calloc((size_t)extra, sizeof(MsValue));
    }
    *ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot) = val;
    return MS_NIL_VAL();
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
    ms_vm_define_native(vm, "clock",   native_clock,    0);
    ms_vm_define_native(vm, "print",   native_print,   -1);
    ms_vm_define_native(vm, "type",    native_type,     1);
    ms_vm_define_native(vm, "str",     native_str,      1);
    ms_vm_define_native(vm, "num",     native_num,      1);
    ms_vm_define_native(vm, "int",     native_int,      1);
    ms_vm_define_native(vm, "float",   native_float,    1);
    ms_vm_define_native(vm, "len",     native_len,      1);
    ms_vm_define_native(vm, "input",   native_input,   -1);
    ms_vm_define_native(vm, "assert",  native_assert,  -1);
    ms_vm_define_native(vm, "hasattr", native_hasattr,  2);
    ms_vm_define_native(vm, "getattr", native_getattr,  2);
    ms_vm_define_native(vm, "setattr", native_setattr,  3);
    ms_vm_define_native(vm, "resume",  native_resume,  -1);
    /* legacy aliases */
    ms_vm_define_native(vm, "tostring", native_str,     1);
    ms_vm_define_native(vm, "toint",    native_int,     1);
    ms_vm_define_native(vm, "tofloat",  native_float,   1);
}
