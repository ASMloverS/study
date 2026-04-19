#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/opcode.h"
#include "ms/vm.h"

static void test_if_else(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "if (true) { print(1) } else { print(2) }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(dc == 0);
    bool found_test = false, found_jmp = false;
    for (int i = 0; i < fn->chunk.code_count; i++) {
        MsOpCode op = (MsOpCode)MS_GET_OP(fn->chunk.code[i]);
        if (op == MS_OP_TEST) found_test = true;
        if (op == MS_OP_JMP)  found_jmp  = true;
    }
    TEST_ASSERT(found_test);
    TEST_ASSERT(found_jmp);
    ms_disasm_chunk(&fn->chunk, "if_else");
    ms_vm_free(&vm);
}

static void test_while_loop(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "var i = 0\nwhile (i < 3) { i = i + 1 }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(dc == 0);
    int jmp_count = 0;
    for (int i = 0; i < fn->chunk.code_count; i++) {
        if ((MsOpCode)MS_GET_OP(fn->chunk.code[i]) == MS_OP_JMP) jmp_count++;
    }
    /* while needs at least 2 JMPs: exit + back-edge */
    TEST_ASSERT(jmp_count >= 2);
    ms_disasm_chunk(&fn->chunk, "while");
    ms_vm_free(&vm);
}

static void test_function_decl(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "fun add(a, b) { return a + b }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT(dc == 0);
    bool found_closure = false;
    for (int i = 0; i < fn->chunk.code_count; i++) {
        if ((MsOpCode)MS_GET_OP(fn->chunk.code[i]) == MS_OP_CLOSURE) {
            found_closure = true;
            break;
        }
    }
    TEST_ASSERT(found_closure);
    /* The inner function constant must have arity 2 */
    bool found_fn_const = false;
    for (int i = 0; i < fn->chunk.constants.count; i++) {
        MsValue v = fn->chunk.constants.data[i];
        if (MS_IS_OBJ_TYPE(v, MS_OBJ_FUNCTION)) {
            MsObjFunction* inner = (MsObjFunction*)MS_AS_OBJECT(v);
            if (inner->arity == 2) { found_fn_const = true; break; }
        }
    }
    TEST_ASSERT(found_fn_const);
    ms_disasm_chunk(&fn->chunk, "fun_decl");
    ms_vm_free(&vm);
}

int main(void) {
    test_if_else();
    test_while_loop();
    test_function_decl();
    printf("test_compiler_stmts: all passed\n");
    return 0;
}
