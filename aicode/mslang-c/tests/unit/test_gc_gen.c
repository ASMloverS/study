#include "test_assert.h"
#include "ms/vm.h"
#include "ms/memory.h"
#include <stdio.h>

static void test_young_gen_collection(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var total = 0\n"
        "var i = 0\n"
        "while (i < 5000) {\n"
        "  var temp = \"x\"\n"
        "  total = total + 1\n"
        "  i = i + 1\n"
        "}\n"
        "print(total)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_gc_promotion(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var long_lived = \"permanent\"\n"
        "var i = 0\n"
        "while (i < 5000) {\n"
        "  var _ = \"temp\"\n"
        "  i = i + 1\n"
        "}\n"
        "print(long_lived)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_gc_closure_survival(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "fun make() {\n"
        "  var x = \"captured\"\n"
        "  return fun() { return x }\n"
        "}\n"
        "var f = make()\n"
        "var i = 0\n"
        "while (i < 5000) { var _ = \"gc_pressure\"\n i = i + 1 }\n"
        "print(f())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_pool_alloc_free(void) {
    MsObjectPool pool;
    ms_pool_init(&pool, sizeof(MsObjUpvalue));
    void* a = ms_pool_alloc(&pool);
    void* b = ms_pool_alloc(&pool);
    TEST_ASSERT(a != NULL);
    TEST_ASSERT(b != NULL);
    TEST_ASSERT(a != b);
    ms_pool_free_obj(&pool, a);
    void* c = ms_pool_alloc(&pool);
    TEST_ASSERT(c == a); /* reuse freed slot */
    ms_pool_destroy(&pool);
}

int main(void) {
    test_young_gen_collection();
    test_gc_promotion();
    test_gc_closure_survival();
    test_pool_alloc_free();
    printf("test_gc_gen: all passed\n");
    return 0;
}
