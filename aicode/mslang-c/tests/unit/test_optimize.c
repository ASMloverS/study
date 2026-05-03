#include "test_assert.h"
#include "ms/optimize.h"
#include "ms/chunk.h"
#include "ms/opcode.h"
#include "ms/value.h"
#include <stdio.h>

static void test_redundant_move(void) {
    MsChunk c;
    ms_chunk_init(&c);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_MOVE, 3, 3, 0), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0), 1, 5);
    ms_peephole_optimize(&c);
    TEST_ASSERT_EQ(c.code_count, 1);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_RETURN);
    ms_chunk_free(&c);
}

static void test_loadk_neg_fold(void) {
    MsChunk c;
    ms_chunk_init(&c);
    int k = ms_chunk_add_constant(&c, MS_INT_VAL(5));
    ms_chunk_write(&c, ms_enc_ABx(MS_OP_LOADK, 0, k), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_NEG, 0, 0, 0), 1, 5);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 2, 0), 1, 9);
    ms_peephole_optimize(&c);
    TEST_ASSERT(MS_AS_INT(c.constants.data[k]) == -5);
    TEST_ASSERT_EQ(c.code_count, 2);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_LOADK);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[1]), MS_OP_RETURN);
    ms_chunk_free(&c);
}

static void test_loadnil_merge(void) {
    MsChunk c;
    ms_chunk_init(&c);
    /* LOADNIL R0 (B=0 means range [A..A]) */
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_LOADNIL, 0, 0, 0), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_LOADNIL, 1, 1, 0), 1, 2);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0), 1, 3);
    ms_peephole_optimize(&c);
    /* Two adjacent LOADNILs merge into one; code_count drops to 2 */
    TEST_ASSERT_EQ(c.code_count, 2);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_LOADNIL);
    TEST_ASSERT_EQ(MS_GET_A(c.code[0]), 0);
    TEST_ASSERT_EQ(MS_GET_B(c.code[0]), 1);
    ms_chunk_free(&c);
}

static void test_dead_code_after_return(void) {
    MsChunk c;
    ms_chunk_init(&c);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_LOADTRUE, 1, 0, 0), 1, 2);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 1, 2, 0), 1, 3);
    ms_peephole_optimize(&c);
    /* Dead code after RETURN should be eliminated; only 1 RETURN left */
    TEST_ASSERT_EQ(c.code_count, 1);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_RETURN);
    ms_chunk_free(&c);
}

static void test_move_return_tail(void) {
    MsChunk c;
    ms_chunk_init(&c);
    /* MOVE R1, R0  then  RETURN R1, 2 */
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_MOVE, 1, 0, 0), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 1, 2, 0), 1, 2);
    ms_peephole_optimize(&c);
    /* Should become RETURN R0, 2 (1 instruction) */
    TEST_ASSERT_EQ(c.code_count, 1);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_RETURN);
    TEST_ASSERT_EQ(MS_GET_A(c.code[0]), 0);
    TEST_ASSERT_EQ(MS_GET_B(c.code[0]), 2);
    ms_chunk_free(&c);
}

static void test_jmp_fixup(void) {
    MsChunk c;
    ms_chunk_init(&c);
    /* MOVE R0,R0  (redundant, becomes NOP)
       JMP +1       (jumps over next instruction to index 3)
       LOADTRUE R1  (live code, target of nothing)
       RETURN R0,1  (jump target at old index 3) */
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_MOVE, 0, 0, 0), 1, 1);
    ms_chunk_write(&c, ms_enc_AsBx(MS_OP_JMP, 0, 1), 1, 2);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_LOADTRUE, 1, 0, 0), 1, 3);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0), 1, 4);
    ms_peephole_optimize(&c);
    /* After NOP compaction: 3 instructions remain: JMP, LOADTRUE, RETURN
       JMP offset should be fixed to still skip LOADTRUE */
    TEST_ASSERT_EQ(c.code_count, 3);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_JMP);
    TEST_ASSERT_EQ(MS_GET_sBx(c.code[0]), 1);
    ms_chunk_free(&c);
}

int main(void) {
    test_redundant_move();
    test_loadk_neg_fold();
    test_loadnil_merge();
    test_dead_code_after_return();
    test_move_return_tail();
    test_jmp_fixup();
    printf("test_optimize: all passed\n");
    return 0;
}
