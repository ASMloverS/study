#include "test_assert.h"
#include "ms/vm.h"
#include "ms/module.h"
#include "ms/dynlib.h"
#include "ms/object.h"
#include "ms/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SAMPLE_EXT_DIR is injected by CMake via -DSAMPLE_EXT_DIR=<path>.
   Fallback keeps clangd happy; real builds always receive the correct path. */
#ifndef SAMPLE_EXT_DIR
#  define SAMPLE_EXT_DIR "."
#endif

/* ---- helpers ---- */

static void clear_env(void) {
#ifdef _WIN32
    _putenv_s("MSLANG_PATH", "");
#else
    unsetenv("MSLANG_PATH");
#endif
}

/* ---- test: dynlib open / sym / close ---- */

static void test_dynlib_open_nonexistent(void) {
    MsDynlib lib = ms_dynlib_open("/nonexistent/path/libno.so");
    TEST_ASSERT(lib == NULL);
}

/* ---- test: ms_vm_track_dynlib ---- */

static void test_vm_track_dynlib_null(void) {
    MsVM vm;
    clear_env();
    ms_vm_init(&vm);
    /* Tracking NULL should be a no-op */
    ms_vm_track_dynlib(&vm, NULL);
    TEST_ASSERT_EQ(vm.dynlib_count, 0);
    ms_vm_free(&vm);
}

/* ---- test: dynamic module load via ms_module_load ---- */

static void test_dynamic_module_load(void) {
    MsVM vm;
    clear_env();
    ms_vm_init(&vm);

    /* Add the directory containing sample_ext to the search path */
    ms_vm_add_search_path(&vm, SAMPLE_EXT_DIR);

    /* ms_module_load with no from_path; should find sample_ext.dll/.so/.dylib */
    MsObjModule* mod = ms_module_load(&vm, "sample_ext", NULL);
    TEST_ASSERT(mod != NULL);
    TEST_ASSERT(mod->state == MS_MOD_INITIALIZED);

    /* The module should export "hello" */
    MsObjString* key = ms_obj_string_copy(&vm, "hello", 5);
    MsValue val;
    bool found = ms_table_get(&mod->exports, key, &val);
    TEST_ASSERT(found);
    TEST_ASSERT(MS_IS_NATIVE(val));

    ms_vm_free(&vm);
}

/* ---- test: second import returns cached module ---- */

static void test_dynamic_module_cached(void) {
    MsVM vm;
    clear_env();
    ms_vm_init(&vm);
    ms_vm_add_search_path(&vm, SAMPLE_EXT_DIR);

    MsObjModule* m1 = ms_module_load(&vm, "sample_ext", NULL);
    MsObjModule* m2 = ms_module_load(&vm, "sample_ext", NULL);
    TEST_ASSERT(m1 != NULL);
    TEST_ASSERT(m1 == m2); /* same pointer: cache hit */

    ms_vm_free(&vm);
}

/* ---- test: module not found → NULL ---- */

static void test_module_not_found(void) {
    MsVM vm;
    clear_env();
    ms_vm_init(&vm);
    /* No search paths → can't find "no_such_module" */
    MsObjModule* mod = ms_module_load(&vm, "no_such_module", NULL);
    TEST_ASSERT(mod == NULL);
    ms_vm_free(&vm);
}

int main(void) {
    test_dynlib_open_nonexistent();
    test_vm_track_dynlib_null();
    test_module_not_found();
    test_dynamic_module_load();
    test_dynamic_module_cached();
    printf("test_capi_dynamic: all passed\n");
    return 0;
}
