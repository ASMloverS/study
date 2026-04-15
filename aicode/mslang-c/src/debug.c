#include "ms/debug.h"
#include "ms/opcode.h"
#include "ms/value.h"
#include <stdio.h>

static void print_rk(const MsChunk* chunk, int rk) {
    if (MS_RK_IS_K(rk)) {
        int ki = MS_RK_TO_K(rk);
        printf("K(%d)", ki);
        if (ki < chunk->constants.count) {
            printf(" ; ");
            ms_value_print(chunk->constants.data[ki]);
        }
    } else {
        printf("R(%d)", rk);
    }
}

static void disasm_ABC(const MsChunk* chunk, MsInstruction instr) {
    int A = MS_GET_A(instr);
    int B = MS_GET_B(instr);
    int C = MS_GET_C(instr);
    printf("R(%d)  ", A);
    print_rk(chunk, B);
    printf(" ");
    print_rk(chunk, C);
    printf("\n");
}

static void disasm_ABx_const(const MsChunk* chunk, MsInstruction instr) {
    int A  = MS_GET_A(instr);
    int Bx = MS_GET_Bx(instr);
    printf("R(%d)  K(%d)", A, Bx);
    if (Bx < chunk->constants.count) {
        printf(" ; ");
        ms_value_print(chunk->constants.data[Bx]);
    }
    printf("\n");
}

static void disasm_AsBx(MsInstruction instr) {
    int sBx = MS_GET_sBx(instr);
    if (sBx >= 0) printf("+%d\n", sBx);
    else          printf("%d\n",  sBx);
}

static void disasm_closure(MsInstruction instr) {
    int A  = MS_GET_A(instr);
    int Bx = MS_GET_Bx(instr);
    printf("R(%d)  proto[%d]\n", A, Bx);
}

static void disasm_call(MsInstruction instr) {
    int A = MS_GET_A(instr);
    int B = MS_GET_B(instr);
    int C = MS_GET_C(instr);
    printf("R(%d)  args=%d rets=%d\n", A, B - 1, C - 1);
}

static void disasm_return(MsInstruction instr) {
    int A = MS_GET_A(instr);
    int B = MS_GET_B(instr);
    printf("R(%d)  count=%d\n", A, B - 1);
}

/* A only: no meaningful B/C operands */
static void disasm_A(MsInstruction instr) {
    int A = MS_GET_A(instr);
    printf("R(%d)\n", A);
}

/* A, B: B is a plain register index (not RK) */
static void disasm_AB(MsInstruction instr) {
    int A = MS_GET_A(instr);
    int B = MS_GET_B(instr);
    printf("R(%d)  R(%d)\n", A, B);
}

int ms_disasm_instr(const MsChunk* chunk, int offset) {
    if (!chunk || offset < 0 || offset >= chunk->code_count) return offset + 1;
    MsInstruction instr = chunk->code[offset];
    MsOpCode op = (MsOpCode)MS_GET_OP(instr);

    int line = ms_chunk_get_line(chunk, offset);
    int prev = offset > 0 ? ms_chunk_get_line(chunk, offset - 1) : -1;

    if (offset == 0 || line != prev)
        printf("%04d %4d %-12s ", offset, line, ms_opcode_name(op));
    else
        printf("%04d    | %-12s ", offset, ms_opcode_name(op));

    switch (op) {
        /* iABx: Bx is a constant-pool index */
        case MS_OP_LOADK:
        case MS_OP_GETGLOBAL:
        case MS_OP_SETGLOBAL:
        case MS_OP_DEFGLOBAL:
        case MS_OP_CLASS:
        case MS_OP_IMPORT:
        case MS_OP_IMPFROM:
        case MS_OP_IMPALIAS:    disasm_ABx_const(chunk, instr);  break;
        case MS_OP_CLOSURE:     disasm_closure(instr);            break;

        /* iAsBx */
        case MS_OP_JMP:
        case MS_OP_TRY:         disasm_AsBx(instr);               break;

        /* iABC: B and C are RK operands */
        case MS_OP_ADD:
        case MS_OP_SUB:
        case MS_OP_MUL:
        case MS_OP_DIV:
        case MS_OP_MOD:
        case MS_OP_EQ:
        case MS_OP_LT:
        case MS_OP_LE:
        case MS_OP_BAND:
        case MS_OP_BOR:
        case MS_OP_BXOR:
        case MS_OP_SHL:
        case MS_OP_SHR:
        case MS_OP_GETPROP:
        case MS_OP_SETPROP:
        case MS_OP_GETSUPER:
        case MS_OP_GETIDX:
        case MS_OP_SETIDX:
        /* TEST/TESTSET: iABC, A=reg, C=bool flag; disasm_ABC shows all fields */
        case MS_OP_TEST:
        case MS_OP_TESTSET:
        case MS_OP_ADD_II:
        case MS_OP_ADD_FF:
        case MS_OP_ADD_SS:
        case MS_OP_SUB_II:
        case MS_OP_SUB_FF:
        case MS_OP_MUL_II:
        case MS_OP_MUL_FF:
        case MS_OP_DIV_FF:
        case MS_OP_LT_II:
        case MS_OP_LT_FF:
        case MS_OP_EQ_II:       disasm_ABC(chunk, instr);          break;

        /* iABC: B is a plain register, C unused */
        case MS_OP_MOVE:
        case MS_OP_GETUPVAL:
        case MS_OP_SETUPVAL:
        case MS_OP_INHERIT:
        case MS_OP_METHOD:
        case MS_OP_STATICMETH:
        case MS_OP_GETTER:
        case MS_OP_SETTER:
        case MS_OP_ABSTMETH:
        case MS_OP_FORITER:
        case MS_OP_YIELD:
        case MS_OP_RESUME:      disasm_AB(instr);                  break;

        /* special multi-field formats */
        case MS_OP_CALL:
        case MS_OP_INVOKE:
        case MS_OP_SUPERINV:    disasm_call(instr);                break;
        case MS_OP_RETURN:      disasm_return(instr);              break;

        /* iABC: A only meaningful */
        case MS_OP_LOADNIL:
        case MS_OP_LOADTRUE:
        case MS_OP_LOADFALSE:
        case MS_OP_NEG:
        case MS_OP_NOT:
        case MS_OP_STR:
        case MS_OP_BNOT:
        case MS_OP_CLOSE:
        case MS_OP_NEWLIST:
        case MS_OP_NEWMAP:
        case MS_OP_NEWTUPLE:
        case MS_OP_THROW:
        case MS_OP_DEFER:
        case MS_OP_ENDTRY:
        case MS_OP_EXTRAARG:
        case MS_OP_NOP:
        default:                disasm_A(instr);                   break;
    }
    return offset + 1;
}

void ms_disasm_chunk(const MsChunk* chunk, const char* name) {
    if (!chunk) return;
    printf("=== %s ===\n", name);
    for (int i = 0; i < chunk->code_count; )
        i = ms_disasm_instr(chunk, i);
}
