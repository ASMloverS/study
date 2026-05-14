#include "ms/optimize.h"
#include "ms/opcode.h"
#include "ms/value.h"
#include <stdlib.h>

/* ---- helpers ---- */

static void set_nop(MsChunk* chunk, int i) {
    chunk->code[i] = ms_enc_ABC(MS_OP_NOP, 0, 0, 0);
}

/* ---- pass 1: redundant MOVE R(A),R(A) -> NOP ---- */

static void pass_redundant_move(MsChunk* chunk) {
    for (int i = 0; i < chunk->code_count; i++) {
        MsInstruction inst = chunk->code[i];
        if (MS_GET_OP(inst) == MS_OP_MOVE && MS_GET_A(inst) == MS_GET_B(inst))
            set_nop(chunk, i);
    }
}

/* ---- pass 2: consecutive LOADNIL merging ---- */

static void pass_loadnil_merge(MsChunk* chunk) {
    for (int i = 0; i < chunk->code_count - 1; i++) {
        if (MS_GET_OP(chunk->code[i]) != MS_OP_LOADNIL) continue;
        if (MS_GET_OP(chunk->code[i + 1]) != MS_OP_LOADNIL) continue;
        int a1 = MS_GET_A(chunk->code[i]);
        int b1 = MS_GET_B(chunk->code[i]);
        int a2 = MS_GET_A(chunk->code[i + 1]);
        int b2 = MS_GET_B(chunk->code[i + 1]);
        if (a2 == b1 + 1) {
            chunk->code[i] = ms_enc_ABC(MS_OP_LOADNIL, a1, b2, 0);
            set_nop(chunk, i + 1);
        }
    }
}

/* ---- pass 3: dead code after RETURN/THROW (not a jump target) -> NOP ---- */

static void pass_dead_code(MsChunk* chunk) {
    int n = chunk->code_count;
    bool* is_target = (bool*)calloc((size_t)n, sizeof(bool));
    if (!is_target) return;

    for (int i = 0; i < n; i++) {
        int op = MS_GET_OP(chunk->code[i]);
        if (op == MS_OP_JMP || op == MS_OP_TRY || op == MS_OP_FORITER) {
            int target = i + 1 + MS_GET_sBx(chunk->code[i]);
            if (target >= 0 && target < n) is_target[target] = true;
        }
        /* TEST/TESTSET skip the next instruction (which is a JMP) */
        if (op == MS_OP_TEST || op == MS_OP_TESTSET) {
            int target = i + 2;
            if (target < n) is_target[target] = true;
        }
    }

    bool dead = false;
    for (int i = 0; i < n; i++) {
        if (is_target[i]) dead = false;
        if (dead) { set_nop(chunk, i); continue; }
        int op = MS_GET_OP(chunk->code[i]);
        if (op == MS_OP_RETURN || op == MS_OP_THROW) dead = true;
    }

    free(is_target);
}

/* ---- pass 4: LOADK R(A) K(n)  +  NEG R(A) R(A)  ->  fold neg into K ---- */

static void pass_loadk_neg_fold(MsChunk* chunk) {
    int n = chunk->code_count;
    for (int i = 0; i < n - 1; i++) {
        MsInstruction inst = chunk->code[i];
        if (MS_GET_OP(inst) != MS_OP_LOADK) continue;
        MsInstruction next = chunk->code[i + 1];
        if (MS_GET_OP(next) != MS_OP_NEG) continue;
        int a_load = MS_GET_A(inst);
        if (MS_GET_A(next) != a_load || MS_GET_B(next) != a_load) continue;
        int kidx = MS_GET_Bx(inst);
        MsValue val = chunk->constants.data[kidx];
        if (MS_IS_NUMBER(val)) {
            chunk->constants.data[kidx] = MS_NUMBER_VAL(-MS_AS_NUMBER(val));
            set_nop(chunk, i + 1);
        } else if (MS_IS_INT(val)) {
            chunk->constants.data[kidx] = MS_INT_VAL(-MS_AS_INT(val));
            set_nop(chunk, i + 1);
        }
    }
}

/* ---- pass 5: MOVE R(A),R(B) + RETURN R(A),2 -> RETURN R(B),2 + NOP ---- */

static void pass_move_return_tail(MsChunk* chunk) {
    int n = chunk->code_count;
    for (int i = 0; i < n - 1; i++) {
        MsInstruction mov = chunk->code[i];
        if (MS_GET_OP(mov) != MS_OP_MOVE) continue;
        MsInstruction ret = chunk->code[i + 1];
        if (MS_GET_OP(ret) != MS_OP_RETURN) continue;
        if (MS_GET_B(ret) != 2) continue;
        if (MS_GET_A(ret) != MS_GET_A(mov)) continue;
        int src = MS_GET_B(mov);
        chunk->code[i + 1] = ms_enc_ABC(MS_OP_RETURN, src, 2, 0);
        set_nop(chunk, i);
    }
}

/* ---- NOP compaction + jump fixup ---- */

static void compact_nops(MsChunk* chunk) {
    int n = chunk->code_count;
    int* offset_map = (int*)malloc(sizeof(int) * (size_t)(n + 1));
    if (!offset_map) return;

    /* Build offset_map: old_idx -> new_idx */
    int new_count = 0;
    for (int i = 0; i < n; i++) {
        offset_map[i] = new_count;
        if (MS_GET_OP(chunk->code[i]) != MS_OP_NOP) new_count++;
    }
    offset_map[n] = new_count; /* sentinel for targets == n */

    /* Fix JMP/TRY sBx offsets; TEST/TESTSET/FORITER skip-next needs no fixup */
    for (int i = 0; i < n; i++) {
        int op = MS_GET_OP(chunk->code[i]);
        if (op != MS_OP_JMP && op != MS_OP_TRY) continue;
        int old_target = i + 1 + MS_GET_sBx(chunk->code[i]);
        if (old_target < 0) old_target = 0;
        if (old_target > n) old_target = n;
        int new_sBx = offset_map[old_target] - offset_map[i] - 1;
        chunk->code[i] = ms_enc_AsBx(op, MS_GET_A(chunk->code[i]), new_sBx);
    }

    /* Compact: remove NOPs in-place */
    new_count = 0;
    for (int i = 0; i < n; i++) {
        if (MS_GET_OP(chunk->code[i]) != MS_OP_NOP)
            chunk->code[new_count++] = chunk->code[i];
    }
    chunk->code_count = new_count;

    /* Update RLE line info: subtract removed counts.
       Simple approach: rebuild count array by mapping old spans. */
    int remaining = new_count;
    for (int r = 0; r < chunk->line_count && remaining > 0; r++) {
        if (chunk->lines[r].count > remaining) chunk->lines[r].count = remaining;
        remaining -= chunk->lines[r].count;
    }

    free(offset_map);
}

/* ---- public entry point ---- */

void ms_peephole_optimize(MsChunk* chunk) {
    if (!chunk || chunk->code_count == 0) return;
    pass_redundant_move(chunk);
    pass_loadnil_merge(chunk);
    pass_dead_code(chunk);
    pass_loadk_neg_fold(chunk);
    pass_move_return_tail(chunk);
    compact_nops(chunk);
}
