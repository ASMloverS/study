#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/opcode.h"
#include "ms/object.h"
#include "ms/value.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- forward decls ---- */
static MsInterpretResult call_value(MsVM* vm, MsValue callee,
                                    int arg_count, int ret_dst);
static MsInterpretResult vm_run_inner(MsVM* vm);
void ms_vm_register_natives(MsVM* vm);

/* ---- init / free ---- */

void ms_vm_init(MsVM* vm) {
    vm->stack_top       = vm->stack;
    vm->frame_count     = 0;
    vm->objects         = NULL;
    vm->bytes_allocated = 0;
    vm->next_gc         = 1024 * 1024;
    vm->open_upvalues   = NULL;
    vm->init_string     = NULL;
    vm->compiler        = NULL;
    vm->gray_stack      = NULL;
    vm->gray_count      = 0;
    vm->gray_capacity   = 0;
    ms_table_init(&vm->globals);
    ms_table_init(&vm->strings);
    ms_vm_register_natives(vm);
}

void ms_vm_free(MsVM* vm) {
    ms_table_free(&vm->globals);
    ms_table_free(&vm->strings);
    free(vm->gray_stack);
    vm->gray_stack    = NULL;
    vm->gray_count    = 0;
    vm->gray_capacity = 0;
    MsObject* obj = vm->objects;
    while (obj) {
        MsObject* next = obj->next;
        ms_object_free(vm, obj);
        obj = next;
    }
    vm->objects = NULL;
}

/* ---- runtime error ---- */

void ms_vm_runtime_error(MsVM* vm, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "RuntimeError: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    for (int i = vm->frame_count - 1; i >= 0; i--) {
        MsCallFrame* frame = &vm->frames[i];
        MsObjFunction* fn = frame->closure->function;
        ptrdiff_t offset = frame->ip - fn->chunk.code - 1;
        int line = ms_chunk_get_line(&fn->chunk, (int)offset);
        const char* name = fn->name ? fn->name->data : "<script>";
        fprintf(stderr, "  [line %d] in %s\n", line, name);
    }
    vm->frame_count = 0;
    vm->stack_top   = vm->stack;
}

/* ---- native define ---- */

void ms_vm_define_native(MsVM* vm, const char* name, MsNativeFn fn, int arity) {
    MsObjNative* nat = ms_obj_native_new(vm, fn, name, arity);
    MsObjString* key = ms_obj_string_copy(vm, name, (int)strlen(name));
    ms_table_set(&vm->globals, key, MS_OBJ_VAL(nat));
}

/* ---- ms_vm_interpret ---- */

MsInterpretResult ms_vm_interpret(MsVM* vm, const char* source, const char* path) {
    MsDiagnostic diags[32];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, source, path, diags, &diag_count, 32);
    if (!fn) {
        for (int i = 0; i < diag_count; i++) {
            fprintf(stderr, "[line %d] Error: %s\n",
                    diags[i].line, diags[i].message);
        }
        return MS_INTERPRET_COMPILE_ERROR;
    }

    MsObjClosure* cl = ms_obj_closure_new(vm, fn);
    MsCallFrame*  frame = &vm->frames[0];
    frame->closure = cl;
    frame->ip      = fn->chunk.code;
    frame->slots   = vm->stack;
    vm->frame_count = 1;
    int need = fn->max_stack_size + 1;
    if (need < 1) need = 1;
    vm->stack_top = vm->stack + need;
    return vm_run_inner(vm);
}

MsInterpretResult ms_vm_run(MsVM* vm) {
    return vm_run_inner(vm);
}

/* ---- arithmetic helpers ---- */

static bool numeric_binop(MsValue b, MsValue c, int op, MsValue* out) {
    if (MS_IS_INT(b) && MS_IS_INT(c)) {
        ms_i64 bi = MS_AS_INT(b), ci = MS_AS_INT(c);
        switch (op) {
        case MS_OP_ADD: *out = MS_INT_VAL(bi + ci); return true;
        case MS_OP_SUB: *out = MS_INT_VAL(bi - ci); return true;
        case MS_OP_MUL: *out = MS_INT_VAL(bi * ci); return true;
        case MS_OP_DIV: *out = MS_NUMBER_VAL((double)bi / (double)ci); return true;
        case MS_OP_MOD:
            if (ci == 0) return false;
            *out = MS_INT_VAL(bi % ci);
            return true;
        default: return false;
        }
    }
    if (MS_IS_NUMERIC(b) && MS_IS_NUMERIC(c)) {
        double bd = ms_as_double(b), cd = ms_as_double(c);
        switch (op) {
        case MS_OP_ADD: *out = MS_NUMBER_VAL(bd + cd); return true;
        case MS_OP_SUB: *out = MS_NUMBER_VAL(bd - cd); return true;
        case MS_OP_MUL: *out = MS_NUMBER_VAL(bd * cd); return true;
        case MS_OP_DIV: *out = MS_NUMBER_VAL(bd / cd);  return true;
        case MS_OP_MOD: *out = MS_NUMBER_VAL(bd - (int)(bd / cd) * cd); return true;
        default: return false;
        }
    }
    return false;
}

static bool cmp_values(MsValue b, MsValue c, int op, bool* result) {
    if (MS_IS_INT(b) && MS_IS_INT(c)) {
        ms_i64 bi = MS_AS_INT(b), ci = MS_AS_INT(c);
        switch (op) {
        case MS_OP_LT: *result = bi < ci;  return true;
        case MS_OP_LE: *result = bi <= ci; return true;
        case MS_OP_EQ: *result = bi == ci; return true;
        default: return false;
        }
    }
    if (MS_IS_NUMERIC(b) && MS_IS_NUMERIC(c)) {
        double bd = ms_as_double(b), cd = ms_as_double(c);
        switch (op) {
        case MS_OP_LT: *result = bd < cd;  return true;
        case MS_OP_LE: *result = bd <= cd; return true;
        case MS_OP_EQ: *result = bd == cd; return true;
        default: return false;
        }
    }
    if (op == MS_OP_EQ) { *result = ms_value_equals(b, c); return true; }
    return false;
}

/* ---- call_value ---- */

static MsInterpretResult call_value(MsVM* vm, MsValue callee,
                                    int arg_count, int ret_dst) {
    if (MS_IS_CLOSURE(callee)) {
        MsObjClosure* cl  = MS_AS_CLOSURE(callee);
        MsObjFunction* fn = cl->function;
        if (fn->min_arity != -1 &&
            (arg_count < fn->min_arity || arg_count > fn->arity)) {
            ms_vm_runtime_error(vm, "Expected %d args but got %d.",
                                fn->arity, arg_count);
            return MS_INTERPRET_RUNTIME_ERROR;
        }
        if (vm->frame_count >= MS_FRAMES_MAX) {
            ms_vm_runtime_error(vm, "Stack overflow.");
            return MS_INTERPRET_RUNTIME_ERROR;
        }
        MsCallFrame* new_frame = &vm->frames[vm->frame_count++];
        new_frame->closure = cl;
        new_frame->ip      = fn->chunk.code;
        /* args are in slots ret_dst+1 .. ret_dst+arg_count of caller frame */
        new_frame->slots = vm->frames[vm->frame_count - 2].slots + ret_dst + 1;
        MsValue* new_top = new_frame->slots + fn->max_stack_size + 1;
        if (new_top > vm->stack_top) vm->stack_top = new_top;
        return MS_INTERPRET_OK;
    }
    if (MS_IS_NATIVE(callee)) {
        MsObjNative* nat = MS_AS_NATIVE(callee);
        MsValue* argv = vm->frames[vm->frame_count - 1].slots + ret_dst + 1;
        MsValue result = nat->function(vm, arg_count, argv);
        vm->frames[vm->frame_count - 1].slots[ret_dst] = result;
        return MS_INTERPRET_OK;
    }
    ms_vm_runtime_error(vm, "Can only call functions.");
    return MS_INTERPRET_RUNTIME_ERROR;
}

/* ---- main dispatch loop ---- */

#define RUNTIME_ERROR(vm, ...) \
    do { ms_vm_runtime_error(vm, __VA_ARGS__); \
         return MS_INTERPRET_RUNTIME_ERROR; } while (0)

static MsInterpretResult vm_run_inner(MsVM* vm) {
    MsCallFrame* frame = &vm->frames[vm->frame_count - 1];

#define READ_INSTR() (*frame->ip++)
#define K(idx)  (frame->closure->function->chunk.constants.data[idx])
#define RK(n)   (MS_RK_IS_K(n) ? K(MS_RK_TO_K(n)) : frame->slots[n])
#define R(n)    (frame->slots[n])

    for (;;) {
        MsInstruction instr = READ_INSTR();
        int op = MS_GET_OP(instr);
        int A  = MS_GET_A(instr);
        int B  = MS_GET_B(instr);
        int C  = MS_GET_C(instr);

        switch (op) {
        case MS_OP_NOP: break;

        case MS_OP_LOADK:
            R(A) = K(MS_GET_Bx(instr));
            break;

        case MS_OP_LOADNIL: {
            int last = A + B;
            for (int i = A; i <= last; i++) frame->slots[i] = MS_NIL_VAL();
            break;
        }

        case MS_OP_LOADTRUE:  R(A) = MS_BOOL_VAL(true);  break;
        case MS_OP_LOADFALSE: R(A) = MS_BOOL_VAL(false); break;
        case MS_OP_MOVE:      R(A) = R(B);                break;

        case MS_OP_GETGLOBAL: {
            MsObjString* name = MS_AS_STRING(K(MS_GET_Bx(instr)));
            MsValue val;
            if (!ms_table_get(&vm->globals, name, &val)) {
                RUNTIME_ERROR(vm, "Undefined variable '%s'.", name->data);
            }
            R(A) = val;
            break;
        }

        case MS_OP_DEFGLOBAL:
        case MS_OP_SETGLOBAL: {
            MsObjString* name = MS_AS_STRING(K(MS_GET_Bx(instr)));
            ms_table_set(&vm->globals, name, R(A));
            break;
        }

        case MS_OP_GETUPVAL:
            R(A) = *frame->closure->upvalues[B]->location;
            break;

        case MS_OP_SETUPVAL:
            *frame->closure->upvalues[B]->location = R(A);
            break;

        case MS_OP_ADD: case MS_OP_SUB: case MS_OP_MUL:
        case MS_OP_DIV: case MS_OP_MOD: {
            MsValue bv = RK(B), cv = RK(C);
            MsValue result = MS_NIL_VAL();
            if (op == MS_OP_ADD && MS_IS_STRING(bv) && MS_IS_STRING(cv)) {
                result = MS_OBJ_VAL(
                    ms_obj_string_concat(vm, MS_AS_STRING(bv), MS_AS_STRING(cv)));
            } else if (!numeric_binop(bv, cv, op, &result)) {
                RUNTIME_ERROR(vm, "Operands must be numbers or strings for arithmetic.");
            }
            R(A) = result;
            break;
        }

        case MS_OP_ADD_II:
            R(A) = MS_INT_VAL(MS_AS_INT(RK(B)) + MS_AS_INT(RK(C)));
            break;
        case MS_OP_ADD_FF:
            R(A) = MS_NUMBER_VAL(MS_AS_NUMBER(RK(B)) + MS_AS_NUMBER(RK(C)));
            break;
        case MS_OP_ADD_SS:
            R(A) = MS_OBJ_VAL(
                ms_obj_string_concat(vm, MS_AS_STRING(RK(B)), MS_AS_STRING(RK(C))));
            break;
        case MS_OP_SUB_II:
            R(A) = MS_INT_VAL(MS_AS_INT(RK(B)) - MS_AS_INT(RK(C)));
            break;
        case MS_OP_SUB_FF:
            R(A) = MS_NUMBER_VAL(MS_AS_NUMBER(RK(B)) - MS_AS_NUMBER(RK(C)));
            break;
        case MS_OP_MUL_II:
            R(A) = MS_INT_VAL(MS_AS_INT(RK(B)) * MS_AS_INT(RK(C)));
            break;
        case MS_OP_MUL_FF:
            R(A) = MS_NUMBER_VAL(MS_AS_NUMBER(RK(B)) * MS_AS_NUMBER(RK(C)));
            break;
        case MS_OP_DIV_FF:
            R(A) = MS_NUMBER_VAL(MS_AS_NUMBER(RK(B)) / MS_AS_NUMBER(RK(C)));
            break;
        case MS_OP_LT_II:
            R(A) = MS_BOOL_VAL(MS_AS_INT(RK(B)) < MS_AS_INT(RK(C)));
            break;
        case MS_OP_LT_FF:
            R(A) = MS_BOOL_VAL(MS_AS_NUMBER(RK(B)) < MS_AS_NUMBER(RK(C)));
            break;
        case MS_OP_EQ_II:
            R(A) = MS_BOOL_VAL(MS_AS_INT(RK(B)) == MS_AS_INT(RK(C)));
            break;

        case MS_OP_NEG:
            if (MS_IS_INT(RK(B)))
                R(A) = MS_INT_VAL(-MS_AS_INT(RK(B)));
            else if (MS_IS_NUMBER(RK(B)))
                R(A) = MS_NUMBER_VAL(-MS_AS_NUMBER(RK(B)));
            else
                RUNTIME_ERROR(vm, "Operand must be a number.");
            break;

        case MS_OP_NOT:
            R(A) = MS_BOOL_VAL(!ms_value_is_truthy(RK(B)));
            break;

        case MS_OP_STR: {
            char* s = ms_value_to_cstring(RK(B));
            R(A) = MS_OBJ_VAL(ms_obj_string_copy(vm, s, (int)strlen(s)));
            free(s);
            break;
        }

        case MS_OP_BAND: {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Bitwise operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) & MS_AS_INT(cv));
            break;
        }
        case MS_OP_BOR: {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Bitwise operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) | MS_AS_INT(cv));
            break;
        }
        case MS_OP_BXOR: {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Bitwise operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) ^ MS_AS_INT(cv));
            break;
        }
        case MS_OP_BNOT: {
            MsValue bv = RK(B);
            if (!MS_IS_INT(bv))
                RUNTIME_ERROR(vm, "Bitwise operand must be integer.");
            R(A) = MS_INT_VAL(~MS_AS_INT(bv));
            break;
        }
        case MS_OP_SHL: {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Shift operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) << MS_AS_INT(cv));
            break;
        }
        case MS_OP_SHR: {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Shift operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) >> MS_AS_INT(cv));
            break;
        }

        case MS_OP_EQ: {
            bool result = false;
            cmp_values(RK(B), RK(C), MS_OP_EQ, &result);
            R(A) = MS_BOOL_VAL(result);
            break;
        }
        case MS_OP_LT: {
            bool result = false;
            if (!cmp_values(RK(B), RK(C), MS_OP_LT, &result))
                RUNTIME_ERROR(vm, "Operands must be numbers for comparison.");
            R(A) = MS_BOOL_VAL(result);
            break;
        }
        case MS_OP_LE: {
            bool result = false;
            if (!cmp_values(RK(B), RK(C), MS_OP_LE, &result))
                RUNTIME_ERROR(vm, "Operands must be numbers for comparison.");
            R(A) = MS_BOOL_VAL(result);
            break;
        }

        case MS_OP_JMP: {
            int offset = MS_GET_sBx(instr);
            frame->ip += offset;
            break;
        }

        case MS_OP_TEST: {
            /* B=0: skip JMP when truthy (if/while/and); B=1: skip JMP when falsy (or) */
            bool truthy = ms_value_is_truthy(R(A));
            if ((bool)B != truthy) frame->ip++;
            break;
        }

        case MS_OP_TESTSET: {
            bool truthy = ms_value_is_truthy(R(B));
            if ((bool)C == truthy) {
                R(A) = R(B);
            } else {
                frame->ip++;
            }
            break;
        }

        case MS_OP_CALL: {
            /* A=callee reg, B=argc, C=first_arg_reg (unused by VM, args follow A) */
            MsInterpretResult cr = call_value(vm, R(A), B, A);
            if (cr != MS_INTERPRET_OK) return cr;
            /* refresh frame pointer after potential new frame push */
            frame = &vm->frames[vm->frame_count - 1];
            break;
        }

        case MS_OP_CLOSURE: {
            MsObjFunction* proto = MS_AS_FUNCTION(K(MS_GET_Bx(instr)));
            MsObjClosure*  cl    = ms_obj_closure_new(vm, proto);
            R(A) = MS_OBJ_VAL(cl);
            for (int i = 0; i < proto->upvalue_count; i++) {
                MsInstruction ea = READ_INSTR();
                int is_local = MS_GET_A(ea);
                int idx      = MS_GET_Bx(ea);
                if (is_local) {
                    cl->upvalues[i] = ms_obj_upvalue_new(vm, &frame->slots[idx]);
                } else {
                    cl->upvalues[i] = frame->closure->upvalues[idx];
                }
            }
            break;
        }

        case MS_OP_CLOSE: {
            MsObjUpvalue* uv = vm->open_upvalues;
            while (uv && uv->location >= &frame->slots[A]) {
                uv->closed   = *uv->location;
                uv->location = &uv->closed;
                uv = uv->next;
            }
            vm->open_upvalues = uv;
            break;
        }

        case MS_OP_RETURN: {
            /* B=0: implicit nil, B=1: nil, B>=2: return R(A) */
            MsValue ret = (B >= 2) ? R(A) : MS_NIL_VAL();
            vm->frame_count--;
            if (vm->frame_count == 0) return MS_INTERPRET_OK;

            /* Recover caller CALL target register */
            MsCallFrame* caller = &vm->frames[vm->frame_count - 1];
            MsInstruction call_instr = *(caller->ip - 1);
            int target = MS_GET_A(call_instr);
            caller->slots[target] = ret;
            frame = caller;
            break;
        }

        case MS_OP_EXTRAARG:
            /* consumed inline by CLOSURE; standalone is a no-op */
            break;

        default:
            /* unimplemented opcode: store nil and continue */
            R(A) = MS_NIL_VAL();
            break;
        }
    }

#undef READ_INSTR
#undef K
#undef RK
#undef R
}
