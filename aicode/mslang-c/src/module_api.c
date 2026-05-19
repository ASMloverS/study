#include "ms/module.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/vtable.h"
#include "ms/vm.h"
#include <stdlib.h>
#include <string.h>

/* ---- value constructors ---- */

static MsValue api_make_nil(void)          { return MS_NIL_VAL(); }
static MsValue api_make_bool(bool b)       { return MS_BOOL_VAL(b); }
static MsValue api_make_int(int64_t i)     { return MS_INT_VAL(i); }
static MsValue api_make_number(double d)   { return MS_NUMBER_VAL(d); }

static MsValue api_make_string(MsVM* vm, const char* s, int len) {
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s, len));
}

static MsValue api_make_list(MsVM* vm) {
    return MS_OBJ_VAL(ms_obj_list_new(vm));
}

static MsValue api_make_map(MsVM* vm) {
    return MS_OBJ_VAL(ms_obj_map_new(vm));
}

static void api_list_push(MsVM* vm, MsValue list, MsValue v) {
    (void)vm;
    if (!MS_IS_LIST(list)) return;
    MsObjList* l = MS_AS_LIST(list);
    MS_ARRAY_PUSH(&l->items, v, MsValue);
}

static void api_map_set(MsVM* vm, MsValue map, MsValue key, MsValue val) {
    (void)vm;
    if (!MS_IS_MAP(map)) return;
    ms_vtable_set(&MS_AS_MAP(map)->table, key, val);
}

/* ---- error ---- */

static MsValue api_raise(MsVM* vm, const char* msg) {
    ms_vm_runtime_error(vm, "%s", msg);
    return MS_NIL_VAL();
}

/* ---- userdata ---- */

static MsValue api_userdata_new(MsVM* vm, size_t bytes,
                                 void (*finalize)(void* data),
                                 void (*mark)(MsVM* vm, void* data),
                                 const char* type_tag) {
    return MS_OBJ_VAL(ms_obj_userdata_new(vm, bytes, finalize, mark, type_tag));
}

static void* api_userdata_data(MsValue v) {
    if (!MS_IS_USERDATA(v)) return NULL;
    return MS_AS_USERDATA(v)->data;
}

static const char* api_userdata_tag(MsValue v) {
    if (!MS_IS_USERDATA(v)) return NULL;
    return MS_AS_USERDATA(v)->type_tag;
}

static bool api_userdata_is(MsValue v, const char* tag) {
    if (!MS_IS_USERDATA(v)) return false;
    const char* t = MS_AS_USERDATA(v)->type_tag;
    if (!t || !tag) return t == tag;
    return strcmp(t, tag) == 0;
}

/* ---- value inspection ---- */

static bool        api_is_nil(MsValue v)    { return MS_IS_NIL(v); }
static bool        api_is_bool(MsValue v)   { return MS_IS_BOOL(v); }
static bool        api_is_int(MsValue v)    { return MS_IS_INT(v); }
static bool        api_is_number(MsValue v) { return MS_IS_NUMBER(v); }
static bool        api_is_string(MsValue v) { return MS_IS_STRING(v); }
static bool        api_val_to_bool(MsValue v) { return MS_AS_BOOL(v); }
static int64_t     api_val_to_int(MsValue v)  { return MS_AS_INT(v); }
static double      api_val_to_number(MsValue v) {
    return MS_IS_INT(v) ? (double)MS_AS_INT(v) : MS_AS_NUMBER(v);
}
static const char* api_val_to_cstring(MsValue v) {
    if (!MS_IS_STRING(v)) return NULL;
    return MS_AS_CSTRING(v);
}
static int api_string_len(MsValue v) {
    if (!MS_IS_STRING(v)) return 0;
    return MS_AS_STRING(v)->length;
}

/* ---- container / reflection ---- */

static bool api_is_list(MsValue v)                  { return MS_IS_LIST(v); }
static bool api_is_map(MsValue v)                   { return MS_IS_MAP(v); }
static bool api_is_tuple(MsValue v)                 { return MS_IS_TUPLE(v); }
static bool api_is_function(MsValue v) {
    return MS_IS_CLOSURE(v) || MS_IS_NATIVE(v) || MS_IS_FUNCTION(v);
}
static bool api_is_userdata(MsValue v, const char* tag) {
    return api_userdata_is(v, tag);
}
static int api_list_len(MsValue v) {
    if (!MS_IS_LIST(v)) return 0;
    return MS_AS_LIST(v)->items.count;
}
static MsValue api_list_get(MsValue v, int idx) {
    if (!MS_IS_LIST(v)) return MS_NIL_VAL();
    MsObjList* l = MS_AS_LIST(v);
    if (idx < 0 || idx >= l->items.count) return MS_NIL_VAL();
    return l->items.data[idx];
}
static bool api_map_get(MsValue v, MsValue key, MsValue* out) {
    if (!MS_IS_MAP(v)) return false;
    return ms_vtable_get(&MS_AS_MAP(v)->table, key, out);
}

/* ---- singleton ---- */

static const MsModuleApi k_api = {
    .version          = MS_MODULE_API_VERSION,
    .def_native       = ms_module_def_native,
    .register_natives = ms_module_register_natives,
    .export_value     = ms_module_export_value,
    .make_nil         = api_make_nil,
    .make_bool        = api_make_bool,
    .make_int         = api_make_int,
    .make_number      = api_make_number,
    .make_string      = api_make_string,
    .make_list        = api_make_list,
    .make_map         = api_make_map,
    .list_push        = api_list_push,
    .map_set          = api_map_set,
    .raise            = api_raise,
    .userdata_new     = api_userdata_new,
    .userdata_data    = api_userdata_data,
    .userdata_tag     = api_userdata_tag,
    .userdata_is      = api_userdata_is,
    .is_nil           = api_is_nil,
    .is_bool          = api_is_bool,
    .is_int           = api_is_int,
    .is_number        = api_is_number,
    .is_string        = api_is_string,
    .val_to_bool      = api_val_to_bool,
    .val_to_int       = api_val_to_int,
    .val_to_number    = api_val_to_number,
    .val_to_cstring   = api_val_to_cstring,
    .string_len       = api_string_len,
    .is_list          = api_is_list,
    .is_map           = api_is_map,
    .is_tuple         = api_is_tuple,
    .is_function      = api_is_function,
    .is_userdata      = api_is_userdata,
    .list_len         = api_list_len,
    .list_get         = api_list_get,
    .map_get          = api_map_get,
};

const MsModuleApi* ms_module_api_get(void) { return &k_api; }
