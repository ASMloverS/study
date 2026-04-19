#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/vm.h"

static void test_constant_folding(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(1 + 2)", "<test>",
                                    diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(diag_count == 0);
    /* Folded 1+2 => LOADK(3); at least 1 constant (the folded value) */
    TEST_ASSERT(fn->chunk.constants.count >= 1);
    ms_disasm_chunk(&fn->chunk, "constant_fold");
    ms_vm_free(&vm);
}

static void test_unary(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(-5)", "<test>",
                                    diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(diag_count == 0);
    ms_disasm_chunk(&fn->chunk, "unary");
    ms_vm_free(&vm);
}

static void test_literal_true_false_nil(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(true)\nprint(false)\nprint(nil)",
                                    "<test>", diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(diag_count == 0);
    ms_vm_free(&vm);
}

static void test_comparison(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(1 < 2)", "<test>",
                                    diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(diag_count == 0);
    ms_vm_free(&vm);
}

static void test_bitwise(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(1 << 3)", "<test>",
                                    diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(diag_count == 0);
    ms_vm_free(&vm);
}

static void test_parse_error(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "+", "<test>", diags, &diag_count, 8);
    TEST_ASSERT(fn == NULL);
    TEST_ASSERT(diag_count > 0);
    ms_vm_free(&vm);
}

int main(void) {
    test_constant_folding();
    test_unary();
    test_literal_true_false_nil();
    test_comparison();
    test_bitwise();
    test_parse_error();
    printf("test_compiler_expr: all passed\n");
    return 0;
}
