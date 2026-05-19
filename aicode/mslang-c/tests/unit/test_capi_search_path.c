#include "test_assert.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/fs_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 1. add/prepend with no env var ---- */

static void test_add_search_path(void) {
    MsVM vm;
    /* Clear MSLANG_PATH so ms_vm_init doesn't pollute the count */
#ifdef _WIN32
    _putenv_s("MSLANG_PATH", "");
#else
    unsetenv("MSLANG_PATH");
#endif
    ms_vm_init(&vm);

    int base = vm.module_search_count; /* whatever env var injected */

    ms_vm_add_search_path(&vm, "/dir/a");
    ms_vm_add_search_path(&vm, "/dir/b");

    TEST_ASSERT_EQ(vm.module_search_count, base + 2);
    TEST_ASSERT_STR_EQ(vm.module_search_paths[base],     "/dir/a");
    TEST_ASSERT_STR_EQ(vm.module_search_paths[base + 1], "/dir/b");

    ms_vm_free(&vm);
}

static void test_prepend_search_path(void) {
    MsVM vm;
#ifdef _WIN32
    _putenv_s("MSLANG_PATH", "");
#else
    unsetenv("MSLANG_PATH");
#endif
    ms_vm_init(&vm);

    ms_vm_add_search_path(&vm, "/base");
    ms_vm_prepend_search_path(&vm, "/first");

    /* /first must be at index 0 */
    TEST_ASSERT(vm.module_search_count >= 2);
    TEST_ASSERT_STR_EQ(vm.module_search_paths[0], "/first");

    ms_vm_free(&vm);
}

/* ---- 2. MSLANG_PATH parsing ---- */

static void test_mslang_path_env(void) {
    MsVM vm;
#ifdef _WIN32
    _putenv_s("MSLANG_PATH", "C:\\dir1;C:\\dir2;C:\\dir3");
    char delim = ';';
    const char* d1 = "C:\\dir1";
    const char* d2 = "C:\\dir2";
    const char* d3 = "C:\\dir3";
#else
    setenv("MSLANG_PATH", "/dir1:/dir2:/dir3", 1);
    char delim = ':';
    const char* d1 = "/dir1";
    const char* d2 = "/dir2";
    const char* d3 = "/dir3";
#endif
    (void)delim;

    ms_vm_init(&vm);

    /* At least 3 entries must exist from MSLANG_PATH */
    TEST_ASSERT(vm.module_search_count >= 3);
    TEST_ASSERT_STR_EQ(vm.module_search_paths[0], d1);
    TEST_ASSERT_STR_EQ(vm.module_search_paths[1], d2);
    TEST_ASSERT_STR_EQ(vm.module_search_paths[2], d3);

    ms_vm_free(&vm);

    /* Clean up env */
#ifdef _WIN32
    _putenv_s("MSLANG_PATH", "");
#else
    unsetenv("MSLANG_PATH");
#endif
}

/* ---- 3. ms_string_split ---- */

static void test_string_split_basic(void) {
    char** parts = NULL;
    int count = ms_string_split("/a:/b:/c", ':', &parts);
    TEST_ASSERT_EQ(count, 3);
    TEST_ASSERT_STR_EQ(parts[0], "/a");
    TEST_ASSERT_STR_EQ(parts[1], "/b");
    TEST_ASSERT_STR_EQ(parts[2], "/c");
    for (int i = 0; i < count; i++) free(parts[i]);
    free(parts);
}

static void test_string_split_single(void) {
    char** parts = NULL;
    int count = ms_string_split("onlyone", ':', &parts);
    TEST_ASSERT_EQ(count, 1);
    TEST_ASSERT_STR_EQ(parts[0], "onlyone");
    free(parts[0]);
    free(parts);
}

static void test_string_split_empty(void) {
    char** parts = NULL;
    int count = ms_string_split("", ':', &parts);
    TEST_ASSERT_EQ(count, 0);
    TEST_ASSERT(parts == NULL);
}

static void test_string_split_semicolon(void) {
    char** parts = NULL;
    int count = ms_string_split("C:\\a;C:\\b", ';', &parts);
    TEST_ASSERT_EQ(count, 2);
    TEST_ASSERT_STR_EQ(parts[0], "C:\\a");
    TEST_ASSERT_STR_EQ(parts[1], "C:\\b");
    for (int i = 0; i < count; i++) free(parts[i]);
    free(parts);
}

/* ---- 4. prepend ordering with multiple calls ---- */

static void test_prepend_order(void) {
    MsVM vm;
#ifdef _WIN32
    _putenv_s("MSLANG_PATH", "");
#else
    unsetenv("MSLANG_PATH");
#endif
    ms_vm_init(&vm);

    /* Prepend in reverse CLI order so final order matches argv order */
    ms_vm_prepend_search_path(&vm, "/last");
    ms_vm_prepend_search_path(&vm, "/first");

    TEST_ASSERT(vm.module_search_count >= 2);
    TEST_ASSERT_STR_EQ(vm.module_search_paths[0], "/first");
    TEST_ASSERT_STR_EQ(vm.module_search_paths[1], "/last");

    ms_vm_free(&vm);
}

int main(void) {
    test_add_search_path();
    test_prepend_search_path();
    test_mslang_path_env();
    test_string_split_basic();
    test_string_split_single();
    test_string_split_empty();
    test_string_split_semicolon();
    test_prepend_order();
    printf("test_capi_search_path: all passed\n");
    return 0;
}
