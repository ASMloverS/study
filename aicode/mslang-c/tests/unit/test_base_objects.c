#include "test_assert.h"
#include "ms/object.h"
#include "ms/chunk.h"
#include "ms/vm.h"
#include <stdio.h>

static void test_function(MsVM* vm) {
    MsObjFunction* fn = ms_obj_function_new(vm);
    TEST_ASSERT(fn->obj.type == MS_OBJ_FUNCTION);
    TEST_ASSERT(fn->arity == 0);
    TEST_ASSERT(fn->min_arity == -1);
    TEST_ASSERT(fn->upvalue_count == 0);
    TEST_ASSERT(fn->max_stack_size == 0);
    TEST_ASSERT(fn->is_generator == false);
    TEST_ASSERT(fn->name == NULL);
    fn->name = ms_obj_string_copy(vm, "add", 3);
    TEST_ASSERT_STR_EQ(fn->name->data, "add");
}

static void test_closure(MsVM* vm) {
    MsObjFunction* fn = ms_obj_function_new(vm);
    fn->upvalue_count = 3;
    MsObjClosure* cl = ms_obj_closure_new(vm, fn);
    TEST_ASSERT(cl->obj.type == MS_OBJ_CLOSURE);
    TEST_ASSERT(cl->function == fn);
    TEST_ASSERT(cl->upvalue_count == 3);
    for (int i = 0; i < 3; i++)
        TEST_ASSERT(cl->upvalues[i] == NULL);
}

static void test_upvalue(MsVM* vm) {
    MsValue slot = MS_INT_VAL(99);
    MsObjUpvalue* uv;
    uv = ms_obj_upvalue_new(vm, &slot);
    TEST_ASSERT(uv->obj.type == MS_OBJ_UPVALUE);
    TEST_ASSERT(uv->location == &slot);
    TEST_ASSERT(MS_IS_NIL(uv->closed));
    TEST_ASSERT(uv->next == NULL);
    uv->closed = slot;
    uv->location = &uv->closed;
    TEST_ASSERT(MS_AS_INT(*uv->location) == 99);
}

static MsValue native_add(struct MsVM* vm_arg, int argc, MsValue* argv) {
    (void)vm_arg;
    if (argc == 2) return MS_INT_VAL(MS_AS_INT(argv[0]) + MS_AS_INT(argv[1]));
    return MS_NIL_VAL();
}

static void test_native(MsVM* vm) {
    MsObjNative* n = ms_obj_native_new(vm, native_add, "add", 2);
    TEST_ASSERT(n->obj.type == MS_OBJ_NATIVE);
    TEST_ASSERT(n->arity == 2);
    MsValue args[] = {MS_INT_VAL(3), MS_INT_VAL(4)};
    MsValue result = n->function(vm, 2, args);
    TEST_ASSERT(MS_AS_INT(result) == 7);
}

static void test_variadic_native(MsVM* vm) {
    MsObjNative* n = ms_obj_native_new(vm, native_add, "varfn", -1);
    TEST_ASSERT(n->arity == -1);
}

int main(void) {
    MsVM vm;
    ms_vm_init(&vm);
    test_function(&vm);
    test_closure(&vm);
    test_upvalue(&vm);
    test_native(&vm);
    test_variadic_native(&vm);
    ms_vm_free(&vm);
    printf("test_base_objects: all passed\n");
    return 0;
}
