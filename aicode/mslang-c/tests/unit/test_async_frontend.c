/* test_async_frontend.c - ASYNC-02: async fun + await compiler front-end tests */
#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/opcode.h"
#include "ms/object.h"
#include "ms/vm.h"

/* Helper: find first constant in fn that is a function, return it (or NULL). */
static MsObjFunction* first_fn_constant(MsObjFunction* outer) {
    for (int i = 0; i < outer->chunk.constants.count; i++) {
        MsValue v = outer->chunk.constants.data[i];
        if (MS_IS_OBJ_TYPE(v, MS_OBJ_FUNCTION))
            return (MsObjFunction*)MS_AS_OBJECT(v);
    }
    return NULL;
}

/* 1. async fun decl compiles without error and sets is_async=true */
static void test_async_fun_is_async_flag(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* top = ms_compile(&vm,
        "async fun foo() { }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(top != NULL);
    TEST_ASSERT_EQ(dc, 0);
    MsObjFunction* fn = first_fn_constant(top);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(fn->is_async == true);
    TEST_ASSERT(fn->is_generator == false);
    ms_vm_free(&vm);
}

/* 2. normal fun is NOT is_async */
static void test_normal_fun_not_async(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* top = ms_compile(&vm,
        "fun bar() { }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(top != NULL);
    TEST_ASSERT_EQ(dc, 0);
    MsObjFunction* fn = first_fn_constant(top);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(fn->is_async == false);
    ms_vm_free(&vm);
}

/* 3. await inside async fun emits OP_AWAIT */
static void test_await_in_async_emits_op_await(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* top = ms_compile(&vm,
        "async fun fetch() { var x = await nil }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(top != NULL);
    TEST_ASSERT_EQ(dc, 0);
    MsObjFunction* fn = first_fn_constant(top);
    TEST_ASSERT(fn != NULL);
    bool found_await = false;
    for (int i = 0; i < fn->chunk.code_count; i++) {
        if ((MsOpCode)MS_GET_OP(fn->chunk.code[i]) == MS_OP_AWAIT) {
            found_await = true;
            break;
        }
    }
    TEST_ASSERT(found_await);
    ms_vm_free(&vm);
}

/* 4. await outside async fun produces a compile error */
static void test_await_outside_async_is_error(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    /* await used in a plain fun */
    ms_compile(&vm,
        "fun bad() { var x = await nil }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(dc > 0);
    ms_vm_free(&vm);
}

/* 5. yield inside async fun produces a compile error */
static void test_yield_inside_async_is_error(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    ms_compile(&vm,
        "async fun bad() { yield 1 }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(dc > 0);
    ms_vm_free(&vm);
}

/* 6. async fun with parameters compiles correctly */
static void test_async_fun_with_params(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* top = ms_compile(&vm,
        "async fun add(a, b) { var x = await nil\nreturn x }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(top != NULL);
    TEST_ASSERT_EQ(dc, 0);
    MsObjFunction* fn = first_fn_constant(top);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(fn->is_async == true);
    TEST_ASSERT_EQ(fn->arity, 2);
    ms_vm_free(&vm);
}

int main(void) {
    test_async_fun_is_async_flag();
    test_normal_fun_not_async();
    test_await_in_async_emits_op_await();
    test_await_outside_async_is_error();
    test_yield_inside_async_is_error();
    test_async_fun_with_params();
    return 0;
}
