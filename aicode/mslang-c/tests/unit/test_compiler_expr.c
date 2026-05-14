#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/opcode.h"
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

/* test_add_rk_emitted: "var x = a; var y = x + 5" must emit ADD_RK (not ADD)
   because the RHS (5) is a constant literal loaded via LOADK. */
static void test_add_rk_emitted(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    /* Force non-foldable: LHS is a variable (not a literal constant), RHS is a literal. */
    MsObjFunction* fn = ms_compile(&vm,
        "fun f(a) { return a + 5 }", "<test>",
        diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(diag_count == 0);
    /* Find the inner function in constants */
    TEST_ASSERT(fn->chunk.constants.count >= 1);
    MsObjFunction* inner = NULL;
    for (int i = 0; i < fn->chunk.constants.count; i++) {
        MsValue v = fn->chunk.constants.data[i];
        if (MS_IS_OBJ_TYPE(v, MS_OBJ_FUNCTION)) {
            inner = MS_AS_FUNCTION(v);
            break;
        }
    }
    TEST_ASSERT(inner != NULL);
    /* Scan bytecode for ADD_RK */
    bool found_add_rk = false;
    for (int i = 0; i < inner->chunk.code_count; i++) {
        if (MS_GET_OP(inner->chunk.code[i]) == MS_OP_ADD_RK) {
            found_add_rk = true;
            break;
        }
    }
    TEST_ASSERT(found_add_rk);
    ms_vm_free(&vm);
}

int main(void) {
    test_constant_folding();
    test_unary();
    test_literal_true_false_nil();
    test_comparison();
    test_bitwise();
    test_parse_error();
    test_add_rk_emitted();
    printf("test_compiler_expr: all passed\n");
    return 0;
}
