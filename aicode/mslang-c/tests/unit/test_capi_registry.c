#include "test_assert.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/value.h"
#include <string.h>
#include <stdio.h>

/* ---- mock module helpers ---- */

static int g_mock_init_count = 0;

static void mock_module_init(MsVM* vm, MsObjModule* mod) {
    g_mock_init_count++;
    /* Populate a single export: "answer" = 42 */
    MsObjString* key = ms_obj_string_copy(vm, "answer", 6);
    ms_table_set(&mod->exports, key, MS_NUMBER_VAL(42.0));
}

/* ---- tests ---- */

/* 1. Register a mock builtin module, load it, verify exports. */
static void test_register_and_load(void) {
    MsVM vm;
    ms_vm_init(&vm);

    g_mock_init_count = 0;
    ms_vm_register_builtin_module(&vm, "mockmod", mock_module_init);

    MsObjModule* mod = ms_module_load(&vm, "mockmod", NULL);
    TEST_ASSERT(mod != NULL);
    TEST_ASSERT(mod->state == MS_MOD_INITIALIZED);

    /* Verify the export "answer" == 42 */
    MsObjString* key = ms_obj_string_copy(&vm, "answer", 6);
    MsValue val;
    TEST_ASSERT(ms_table_get(&mod->exports, key, &val));
    TEST_ASSERT(MS_IS_NUMBER(val));
    TEST_ASSERT(MS_AS_NUMBER(val) == 42.0);

    TEST_ASSERT_EQ(g_mock_init_count, 1);

    ms_vm_free(&vm);
}

/* 2. Loading the same builtin module twice must only call init once. */
static void test_init_called_once(void) {
    MsVM vm;
    ms_vm_init(&vm);

    g_mock_init_count = 0;
    ms_vm_register_builtin_module(&vm, "once_mod", mock_module_init);

    MsObjModule* m1 = ms_module_load(&vm, "once_mod", NULL);
    MsObjModule* m2 = ms_module_load(&vm, "once_mod", NULL);

    TEST_ASSERT(m1 != NULL);
    TEST_ASSERT(m2 != NULL);
    TEST_ASSERT(m1 == m2);               /* same cached pointer */
    TEST_ASSERT_EQ(g_mock_init_count, 1); /* init called exactly once */

    ms_vm_free(&vm);
}

/* 3. Builtin module takes priority over a same-named filesystem module. */
static void test_builtin_priority_over_fs(void) {
    MsVM vm;
    ms_vm_init(&vm);

    /* Write a filesystem module named "pri_mod.ms" that sets a different value */
    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, "_capi_pri_mod.ms", "w");
#else
    f = fopen("_capi_pri_mod.ms", "w");
#endif
    TEST_ASSERT(f != NULL);
    /* This file would export "answer = 99" if loaded from disk */
    fprintf(f, "var answer = 99\n");
    fclose(f);

    /* Register a builtin with the same bare name */
    g_mock_init_count = 0;
    ms_vm_register_builtin_module(&vm, "_capi_pri_mod", mock_module_init);

    /* Loading by bare name must hit the builtin, not the file */
    MsObjModule* mod = ms_module_load(&vm, "_capi_pri_mod", NULL);
    TEST_ASSERT(mod != NULL);
    TEST_ASSERT_EQ(g_mock_init_count, 1);

    MsObjString* key = ms_obj_string_copy(&vm, "answer", 6);
    MsValue val;
    TEST_ASSERT(ms_table_get(&mod->exports, key, &val));
    TEST_ASSERT(MS_AS_NUMBER(val) == 42.0); /* builtin value, not 99 */

    ms_vm_free(&vm);
    remove("_capi_pri_mod.ms");
}

/* 4. find returns NULL for unregistered names. */
static void test_find_missing(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsBuiltinModuleInit fn = ms_vm_find_builtin_module(&vm, "does_not_exist");
    TEST_ASSERT(fn == NULL);

    ms_vm_free(&vm);
}

int main(void) {
    test_register_and_load();
    test_init_called_once();
    test_builtin_priority_over_fs();
    test_find_missing();
    printf("test_capi_registry: all passed\n");
    return 0;
}
