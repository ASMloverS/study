#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/opcode.h"
#include "ms/vm.h"

static void test_global_var(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    MsObjFunction* fn = ms_compile(&vm, "var x = 10\nprint(x)", "<test>",
                                    diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(dc == 0);
    ms_disasm_chunk(&fn->chunk, "global_var");
    /* DEFGLOBAL must appear in the chunk */
    bool found_defglobal = false;
    for (int i = 0; i < fn->chunk.code_count; i++) {
        if (MS_GET_OP(fn->chunk.code[i]) == MS_OP_DEFGLOBAL) {
            found_defglobal = true;
            break;
        }
    }
    TEST_ASSERT(found_defglobal);
    ms_vm_free(&vm);
}

static void test_global_assign(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    MsObjFunction* fn = ms_compile(&vm, "var x = 1\nx = 2", "<test>",
                                    diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(dc == 0);
    bool found_setglobal = false;
    for (int i = 0; i < fn->chunk.code_count; i++) {
        if (MS_GET_OP(fn->chunk.code[i]) == MS_OP_SETGLOBAL) {
            found_setglobal = true;
            break;
        }
    }
    TEST_ASSERT(found_setglobal);
    ms_vm_free(&vm);
}

static void test_local_scope(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    MsObjFunction* fn = ms_compile(&vm, "{ var a = 1\n var b = 2 }", "<test>",
                                    diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(dc == 0);
    ms_disasm_chunk(&fn->chunk, "local_scope");
    /* No DEFGLOBAL: locals use registers, not globals table */
    for (int i = 0; i < fn->chunk.code_count; i++) {
        TEST_ASSERT(MS_GET_OP(fn->chunk.code[i]) != MS_OP_DEFGLOBAL);
    }
    ms_vm_free(&vm);
}

static void test_compound_assign(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    MsObjFunction* fn = ms_compile(&vm, "var n = 10\nn += 5", "<test>",
                                    diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(dc == 0);
    bool found_add = false;
    for (int i = 0; i < fn->chunk.code_count; i++) {
        if (MS_GET_OP(fn->chunk.code[i]) == MS_OP_ADD) {
            found_add = true;
            break;
        }
    }
    TEST_ASSERT(found_add);
    ms_vm_free(&vm);
}

static void test_local_shadow(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    /* outer global 's', inner local 's' - must compile cleanly */
    MsObjFunction* fn = ms_compile(
        &vm, "var s = \"outer\"\n{ var s = \"inner\" }", "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(dc == 0);
    ms_vm_free(&vm);
}

int main(void) {
    test_global_var();
    test_global_assign();
    test_local_scope();
    test_compound_assign();
    test_local_shadow();
    printf("test_compiler_vars: all passed\n");
    return 0;
}
