#pragma once
#include "ms/common.h"

typedef ms_u32 MsInstruction;

/* iABC:  [B:8][C:8][A:8][Op:8]
   iABx:  [Bx:16][A:8][Op:8]
   iAsBx: [sBx:16][A:8][Op:8]  signed, biased by 32767 */

#define MS_GET_OP(i)   ((int)((i) & 0xFF))
#define MS_GET_A(i)    ((int)(((i) >> 8)  & 0xFF))
#define MS_GET_B(i)    ((int)(((i) >> 24) & 0xFF))
#define MS_GET_C(i)    ((int)(((i) >> 16) & 0xFF))
#define MS_GET_Bx(i)   ((int)(((i) >> 16) & 0xFFFF))
#define MS_GET_sBx(i)  (MS_GET_Bx(i) - 32767)

#define MS_RK_IS_K(rk)  ((rk) >= 128)
#define MS_RK_TO_K(rk)  ((rk) - 128)
#define MS_K_TO_RK(k)   ((k)  + 128)
#define MS_sBx_BIAS      32767

static inline MsInstruction ms_enc_ABC(int op, int A, int B, int C) {
    return (MsInstruction)((ms_u32)op | ((ms_u32)A << 8) |
           ((ms_u32)C << 16) | ((ms_u32)B << 24));
}
static inline MsInstruction ms_enc_ABx(int op, int A, int Bx) {
    return (MsInstruction)((ms_u32)op | ((ms_u32)A << 8) | ((ms_u32)Bx << 16));
}
static inline MsInstruction ms_enc_AsBx(int op, int A, int sBx) {
    return ms_enc_ABx(op, A, sBx + MS_sBx_BIAS);
}

typedef enum {
    MS_OP_LOADK, MS_OP_LOADNIL, MS_OP_LOADTRUE, MS_OP_LOADFALSE, MS_OP_MOVE,
    MS_OP_GETGLOBAL, MS_OP_SETGLOBAL, MS_OP_DEFGLOBAL,
    MS_OP_GETUPVAL, MS_OP_SETUPVAL,
    MS_OP_GETPROP, MS_OP_SETPROP, MS_OP_GETSUPER, MS_OP_EXTRAARG,
    MS_OP_ADD, MS_OP_SUB, MS_OP_MUL, MS_OP_DIV, MS_OP_MOD,
    MS_OP_EQ, MS_OP_LT, MS_OP_LE,
    MS_OP_NEG, MS_OP_NOT, MS_OP_STR,
    MS_OP_BAND, MS_OP_BOR, MS_OP_BXOR, MS_OP_BNOT, MS_OP_SHL, MS_OP_SHR,
    MS_OP_JMP, MS_OP_TEST, MS_OP_TESTSET,
    MS_OP_CALL, MS_OP_INVOKE, MS_OP_SUPERINV, MS_OP_RETURN,
    MS_OP_CLOSURE, MS_OP_CLOSE,
    MS_OP_CLASS, MS_OP_INHERIT, MS_OP_METHOD, MS_OP_STATICMETH,
    MS_OP_GETTER, MS_OP_SETTER, MS_OP_ABSTMETH,
    MS_OP_NEWLIST, MS_OP_NEWMAP, MS_OP_NEWTUPLE, MS_OP_GETIDX, MS_OP_SETIDX,
    MS_OP_IMPORT, MS_OP_IMPFROM, MS_OP_IMPALIAS,
    MS_OP_FORITER,
    MS_OP_THROW, MS_OP_TRY, MS_OP_ENDTRY,
    MS_OP_DEFER,
    MS_OP_YIELD, MS_OP_RESUME,
    MS_OP_ADD_II, MS_OP_ADD_FF, MS_OP_ADD_SS,
    MS_OP_SUB_II, MS_OP_SUB_FF,
    MS_OP_MUL_II, MS_OP_MUL_FF,
    MS_OP_DIV_FF,
    MS_OP_LT_II, MS_OP_LT_FF,
    MS_OP_EQ_II,
    MS_OP_NOP,
    MS_OP_COUNT,
} MsOpCode;

const char* ms_opcode_name(MsOpCode op);
