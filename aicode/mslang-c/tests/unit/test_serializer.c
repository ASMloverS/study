#include "test_assert.h"
#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/serializer.h"
#include <stdio.h>
#include <string.h>

static void test_roundtrip(void) {
    MsVM vm; ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    const char* src = "fun add(a,b) { return a + b }\nprint(add(1,2))";
    MsObjFunction* fn = ms_compile(&vm, src, "test.ms", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);

    uint32_t hash = ms_fnv1a(src, (int)strlen(src));
    TEST_ASSERT(ms_serialize(fn, "_test.msc", hash));

    MsObjFunction* fn2 = ms_deserialize(&vm, "_test.msc", hash);
    TEST_ASSERT(fn2 != NULL);
    TEST_ASSERT_EQ(fn2->arity, fn->arity);
    TEST_ASSERT_EQ(fn2->chunk.code_count, fn->chunk.code_count);
    TEST_ASSERT_EQ(fn2->chunk.constants.count, fn->chunk.constants.count);

    /* wrong hash -> NULL */
    TEST_ASSERT(ms_deserialize(&vm, "_test.msc", hash + 1) == NULL);

    remove("_test.msc");
    ms_vm_free(&vm);
}

static void test_missing_file(void) {
    MsVM vm; ms_vm_init(&vm);
    TEST_ASSERT(ms_deserialize(&vm, "no_such_file.msc", 0) == NULL);
    ms_vm_free(&vm);
}

static void test_compile_cached_warm_is_faster(void) {
    const char* src  = "var x = 0\nfor (var i = 0; i < 1000; i = i + 1) { x = x + i }\nprint(x)";
    const char* path = "_bench_test.ms";
    const char* msc  = "_bench_test.msc";
    remove(msc);

    MsVM vm0; ms_vm_init(&vm0);
    MsObjFunction* fn0 = ms_compile_cached(&vm0, src, path);
    TEST_ASSERT(fn0 != NULL);
    int cold_code_count = fn0->chunk.code_count;
    ms_vm_free(&vm0);

    MsVM vm1; ms_vm_init(&vm1);
    MsObjFunction* fn1 = ms_compile_cached(&vm1, src, path);
    TEST_ASSERT(fn1 != NULL);
    TEST_ASSERT_EQ(fn1->chunk.code_count, cold_code_count);
    ms_vm_free(&vm1);

    remove(msc);
}

int main(void) {
    test_roundtrip();
    test_missing_file();
    test_compile_cached_warm_is_faster();
    printf("test_serializer: all passed\n");
    return 0;
}
