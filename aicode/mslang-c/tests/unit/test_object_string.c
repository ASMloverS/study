#include "test_assert.h"
#include "ms/object.h"
#include "ms/vm.h"
#include <stdio.h>

static void test_fnv1a(void) {
    uint32_t h1 = ms_fnv1a("hello", 5);
    uint32_t h2 = ms_fnv1a("hello", 5);
    uint32_t h3 = ms_fnv1a("world", 5);
    TEST_ASSERT(h1 == h2);
    TEST_ASSERT(h1 != h3);
    TEST_ASSERT(h1 != 0);
}

static void test_string_copy_basic(MsVM* vm) {
    MsObjString* s = ms_obj_string_copy(vm, "hello", 5);
    TEST_ASSERT(s != NULL);
    TEST_ASSERT(s->length == 5);
    TEST_ASSERT_STR_EQ(s->data, "hello");
    TEST_ASSERT(s->obj.type == MS_OBJ_STRING);
}

static void test_string_interning(MsVM* vm) {
    MsObjString* a = ms_obj_string_copy(vm, "hello", 5);
    MsObjString* b = ms_obj_string_copy(vm, "hello", 5);
    TEST_ASSERT(a == b);
}

static void test_string_different_not_interned(MsVM* vm) {
    MsObjString* a = ms_obj_string_copy(vm, "foo", 3);
    MsObjString* b = ms_obj_string_copy(vm, "bar", 3);
    TEST_ASSERT(a != b);
}

static void test_string_concat(MsVM* vm) {
    MsObjString* a = ms_obj_string_copy(vm, "foo", 3);
    MsObjString* b = ms_obj_string_copy(vm, "bar", 3);
    MsObjString* c = ms_obj_string_concat(vm, a, b);
    TEST_ASSERT(c != NULL);
    TEST_ASSERT(c->length == 6);
    TEST_ASSERT_STR_EQ(c->data, "foobar");
}

static void test_concat_is_interned(MsVM* vm) {
    MsObjString* a = ms_obj_string_copy(vm, "foo", 3);
    MsObjString* b = ms_obj_string_copy(vm, "bar", 3);
    MsObjString* c1 = ms_obj_string_concat(vm, a, b);
    MsObjString* c2 = ms_obj_string_copy(vm, "foobar", 6);
    TEST_ASSERT(c1 == c2);
}

static void test_string_empty(MsVM* vm) {
    MsObjString* s = ms_obj_string_copy(vm, "", 0);
    TEST_ASSERT(s != NULL);
    TEST_ASSERT(s->length == 0);
    TEST_ASSERT(s->data[0] == '\0');
}

static void test_ms_is_string_macro(MsVM* vm) {
    MsObjString* s = ms_obj_string_copy(vm, "test", 4);
    MsValue v = MS_OBJ_VAL(s);
    TEST_ASSERT(MS_IS_STRING(v));
    TEST_ASSERT_STR_EQ(MS_AS_CSTRING(v), "test");
}

static void test_object_in_gc_list(MsVM* vm) {
    MsObject* before = vm->objects;
    ms_obj_string_copy(vm, "newstring", 9);
    TEST_ASSERT(vm->objects != before || before == NULL);
}

int main(void) {
    MsVM vm;
    ms_vm_init(&vm);

    test_fnv1a();
    test_string_copy_basic(&vm);
    test_string_interning(&vm);
    test_string_different_not_interned(&vm);
    test_string_concat(&vm);
    test_concat_is_interned(&vm);
    test_string_empty(&vm);
    test_ms_is_string_macro(&vm);
    test_object_in_gc_list(&vm);

    ms_vm_free(&vm);
    printf("test_object_string: all passed\n");
    return 0;
}
