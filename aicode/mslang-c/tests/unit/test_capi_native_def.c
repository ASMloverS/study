#include "test_assert.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/value.h"
#include <stdio.h>
#include <string.h>

/* ---- dummy native functions ---- */

static MsValue native_double(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(MS_AS_NUMBER(argv[0]) * 2.0);
}

static MsValue native_triple(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(MS_AS_NUMBER(argv[0]) * 3.0);
}

static MsValue native_variadic(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argv;
    return MS_NUMBER_VAL((double)argc);
}

/* ---- helpers ---- */

static MsObjModule* make_module(MsVM* vm, const char* name) {
    MsObjString* n = ms_obj_string_copy(vm, name, (int)strlen(name));
    MsObjString* p = ms_obj_string_copy(vm, "<test>", 6);
    return ms_obj_module_new(vm, n, p);
}

/* ---- tests ---- */

/* 1. ms_module_register_natives: table-style; exports queryable by name. */
static void test_register_natives_table(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsObjModule* mod = make_module(&vm, "mymod");

    static const MsNativeDef defs[] = {
        {"double", native_double, 1},
        {"triple", native_triple, 1},
        {NULL,     NULL,          0},
    };
    ms_module_register_natives(&vm, mod, defs);

    /* "double" must be present and be a native with arity 1 */
    MsObjString* key = ms_obj_string_copy(&vm, "double", 6);
    MsValue val;
    TEST_ASSERT(ms_table_get(&mod->exports, key, &val));
    TEST_ASSERT(MS_IS_NATIVE(val));
    TEST_ASSERT_EQ(MS_AS_NATIVE(val)->arity, 1);

    /* "triple" must be present */
    key = ms_obj_string_copy(&vm, "triple", 6);
    TEST_ASSERT(ms_table_get(&mod->exports, key, &val));
    TEST_ASSERT(MS_IS_NATIVE(val));
    TEST_ASSERT_EQ(MS_AS_NATIVE(val)->arity, 1);

    ms_vm_free(&vm);
}

/* 2. Sentinel {NULL,NULL,0} terminates correctly - no crash, no extra entries. */
static void test_sentinel_terminates(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsObjModule* mod = make_module(&vm, "sentmod");

    static const MsNativeDef defs[] = {
        {"only_one", native_double, 1},
        {NULL, NULL, 0},
    };
    ms_module_register_natives(&vm, mod, defs);

    /* Only "only_one" should exist */
    MsObjString* key = ms_obj_string_copy(&vm, "only_one", 8);
    MsValue val;
    TEST_ASSERT(ms_table_get(&mod->exports, key, &val));
    TEST_ASSERT(MS_IS_NATIVE(val));

    /* Empty table (just sentinel) must not crash */
    static const MsNativeDef empty[] = {
        {NULL, NULL, 0},
    };
    ms_module_register_natives(&vm, mod, empty);

    ms_vm_free(&vm);
}

/* 3. ms_module_export_value: constant value retrievable from .ms side (exports table). */
static void test_export_value(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsObjModule* mod = make_module(&vm, "constmod");
    ms_module_export_value(&vm, mod, "MAGIC", MS_NUMBER_VAL(42.0));

    MsObjString* key = ms_obj_string_copy(&vm, "MAGIC", 5);
    MsValue val;
    TEST_ASSERT(ms_table_get(&mod->exports, key, &val));
    TEST_ASSERT(MS_IS_NUMBER(val));
    TEST_ASSERT(MS_AS_NUMBER(val) == 42.0);

    ms_vm_free(&vm);
}

/* 4. arity=-1 variadic: registered correctly, arity stored as -1. */
static void test_variadic_arity(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsObjModule* mod = make_module(&vm, "varmod");
    ms_module_def_native(&vm, mod, "varfn", native_variadic, -1);

    MsObjString* key = ms_obj_string_copy(&vm, "varfn", 5);
    MsValue val;
    TEST_ASSERT(ms_table_get(&mod->exports, key, &val));
    TEST_ASSERT(MS_IS_NATIVE(val));
    TEST_ASSERT_EQ(MS_AS_NATIVE(val)->arity, -1);

    ms_vm_free(&vm);
}

int main(void) {
    test_register_natives_table();
    test_sentinel_terminates();
    test_export_value();
    test_variadic_arity();
    printf("test_capi_native_def: all passed\n");
    return 0;
}
