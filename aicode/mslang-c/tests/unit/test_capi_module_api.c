#include "test_assert.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── helpers ──────────────────────────────────────────────────── */

static MsObjModule* make_module(MsVM* vm, const char* name) {
    MsObjString* n = ms_obj_string_copy(vm, name, (int)strlen(name));
    MsObjString* p = ms_obj_string_copy(vm, "<test>", 6);
    return ms_obj_module_new(vm, n, p);
}

/* ── 1. singleton not NULL, version correct ───────────────────── */

static void test_singleton(void) {
    const MsModuleApi* api = ms_module_api_get();
    TEST_ASSERT(api != NULL);
    TEST_ASSERT_EQ((int)api->version, MS_MODULE_API_VERSION);
    /* second call returns same pointer */
    TEST_ASSERT(ms_module_api_get() == api);
}

/* ── 2. value constructors round-trip ─────────────────────────── */

static void test_value_constructors(void) {
    MsVM vm;
    ms_vm_init(&vm);
    const MsModuleApi* api = ms_module_api_get();

    MsValue vnil = api->make_nil();
    TEST_ASSERT(api->is_nil(vnil));

    MsValue vtrue = api->make_bool(true);
    TEST_ASSERT(api->is_bool(vtrue));
    TEST_ASSERT(api->val_to_bool(vtrue) == true);

    MsValue vfalse = api->make_bool(false);
    TEST_ASSERT(api->is_bool(vfalse));
    TEST_ASSERT(api->val_to_bool(vfalse) == false);

    MsValue vi = api->make_int(42);
    TEST_ASSERT(api->is_int(vi));
    TEST_ASSERT_EQ(api->val_to_int(vi), (int64_t)42);

    MsValue vn = api->make_number(3.14);
    TEST_ASSERT(api->is_number(vn));
    TEST_ASSERT(api->val_to_number(vn) == 3.14);

    MsValue vs = api->make_string(&vm, "hello", 5);
    TEST_ASSERT(api->is_string(vs));
    TEST_ASSERT_STR_EQ(api->val_to_cstring(vs), "hello");
    TEST_ASSERT_EQ(api->string_len(vs), 5);

    ms_vm_free(&vm);
}

/* ── 3. list constructor + push + reflection ──────────────────── */

static void test_list(void) {
    MsVM vm;
    ms_vm_init(&vm);
    const MsModuleApi* api = ms_module_api_get();

    MsValue lst = api->make_list(&vm);
    TEST_ASSERT(api->is_list(lst));
    TEST_ASSERT_EQ(api->list_len(lst), 0);

    api->list_push(&vm, lst, api->make_int(10));
    api->list_push(&vm, lst, api->make_int(20));
    TEST_ASSERT_EQ(api->list_len(lst), 2);
    TEST_ASSERT_EQ(api->val_to_int(api->list_get(lst, 0)), (int64_t)10);
    TEST_ASSERT_EQ(api->val_to_int(api->list_get(lst, 1)), (int64_t)20);
    /* out-of-bounds returns nil */
    TEST_ASSERT(api->is_nil(api->list_get(lst, 99)));

    ms_vm_free(&vm);
}

/* ── 4. map constructor + set + get ──────────────────────────── */

static void test_map(void) {
    MsVM vm;
    ms_vm_init(&vm);
    const MsModuleApi* api = ms_module_api_get();

    MsValue map = api->make_map(&vm);
    TEST_ASSERT(api->is_map(map));

    MsValue key = api->make_string(&vm, "x", 1);
    MsValue val = api->make_number(99.0);
    api->map_set(&vm, map, key, val);

    MsValue out;
    TEST_ASSERT(api->map_get(map, key, &out));
    TEST_ASSERT(api->is_number(out));
    TEST_ASSERT(api->val_to_number(out) == 99.0);

    /* missing key returns false */
    MsValue missing = api->make_string(&vm, "nope", 4);
    TEST_ASSERT(!api->map_get(map, missing, &out));

    ms_vm_free(&vm);
}

/* ── 5. userdata lifecycle ────────────────────────────────────── */

static int g_finalize_called = 0;
static void ud_finalize(void* data) {
    (void)data;
    g_finalize_called++;
}

static void test_userdata(void) {
    MsVM vm;
    ms_vm_init(&vm);
    const MsModuleApi* api = ms_module_api_get();

    g_finalize_called = 0;

    MsValue ud = api->userdata_new(&vm, 8, ud_finalize, NULL, "TestHandle");
    TEST_ASSERT(!api->is_nil(ud));
    TEST_ASSERT(api->is_userdata(ud, "TestHandle"));
    TEST_ASSERT(!api->is_userdata(ud, "OtherHandle"));
    TEST_ASSERT_STR_EQ(api->userdata_tag(ud), "TestHandle");

    void* ptr = api->userdata_data(ud);
    TEST_ASSERT(ptr != NULL);

    /* write + read back through the data pointer */
    *(int64_t*)ptr = 0xDEADBEEF;
    TEST_ASSERT(*(int64_t*)api->userdata_data(ud) == (int64_t)0xDEADBEEF);

    ms_vm_free(&vm);
    /* finalize must have been called during GC sweep in ms_vm_free */
    TEST_ASSERT_EQ(g_finalize_called, 1);
}

/* ── 6. registration via api->def_native / api->register_natives ─ */

static MsValue native_add(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    return MS_NUMBER_VAL(MS_AS_NUMBER(argv[0]) + MS_AS_NUMBER(argv[1]));
}

static void test_registration(void) {
    MsVM vm;
    ms_vm_init(&vm);
    const MsModuleApi* api = ms_module_api_get();

    MsObjModule* mod = make_module(&vm, "testmod");

    api->def_native(&vm, mod, "add", native_add, 2);

    MsObjString* key = ms_obj_string_copy(&vm, "add", 3);
    MsValue val;
    TEST_ASSERT(ms_table_get(&mod->exports, key, &val));
    TEST_ASSERT(MS_IS_NATIVE(val));
    TEST_ASSERT_EQ(MS_AS_NATIVE(val)->arity, 2);

    /* export_value */
    api->export_value(&vm, mod, "PI", api->make_number(3.14159));
    MsObjString* pikey = ms_obj_string_copy(&vm, "PI", 2);
    MsValue piv;
    TEST_ASSERT(ms_table_get(&mod->exports, pikey, &piv));
    TEST_ASSERT(api->is_number(piv));

    ms_vm_free(&vm);
}

/* ── 7. type predicates on wrong types return false ───────────── */

static void test_type_predicates(void) {
    const MsModuleApi* api = ms_module_api_get();

    MsValue vnil = api->make_nil();
    TEST_ASSERT(!api->is_bool(vnil));
    TEST_ASSERT(!api->is_int(vnil));
    TEST_ASSERT(!api->is_number(vnil));
    TEST_ASSERT(!api->is_string(vnil));
    TEST_ASSERT(!api->is_list(vnil));
    TEST_ASSERT(!api->is_map(vnil));
    TEST_ASSERT(!api->is_tuple(vnil));
    TEST_ASSERT(!api->is_function(vnil));
    TEST_ASSERT(!api->is_userdata(vnil, "any"));

    MsValue vi = api->make_int(1);
    TEST_ASSERT(!api->is_nil(vi));
    TEST_ASSERT(!api->is_bool(vi));
    TEST_ASSERT(!api->is_number(vi));
}

/* ── main ─────────────────────────────────────────────────────── */

int main(void) {
    test_singleton();
    test_value_constructors();
    test_list();
    test_map();
    test_userdata();
    test_registration();
    test_type_predicates();
    printf("test_capi_module_api: all passed\n");
    return 0;
}
