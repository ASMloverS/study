#include "test_assert.h"
#include "ms/chunk.h"
#include <stdio.h>

static void test_encode_decode_ABC(void) {
    MsInstruction i = ms_enc_ABC(MS_OP_ADD, 3, 128, 5);
    TEST_ASSERT_EQ(MS_GET_OP(i), MS_OP_ADD);
    TEST_ASSERT_EQ(MS_GET_A(i), 3);
    TEST_ASSERT_EQ(MS_GET_B(i), 128);
    TEST_ASSERT_EQ(MS_GET_C(i), 5);
    TEST_ASSERT(MS_RK_IS_K(128));
    TEST_ASSERT_EQ(MS_RK_TO_K(128), 0);
}

static void test_encode_decode_AsBx(void) {
    MsInstruction i = ms_enc_AsBx(MS_OP_JMP, 0, -100);
    TEST_ASSERT_EQ(MS_GET_OP(i), MS_OP_JMP);
    TEST_ASSERT_EQ(MS_GET_sBx(i), -100);
    MsInstruction j = ms_enc_AsBx(MS_OP_JMP, 0, 200);
    TEST_ASSERT_EQ(MS_GET_sBx(j), 200);
    MsInstruction k = ms_enc_AsBx(MS_OP_JMP, 0, 0);
    TEST_ASSERT_EQ(MS_GET_sBx(k), 0);
}

static void test_chunk_write_and_constants(void) {
    MsChunk c;
    ms_chunk_init(&c);
    int idx = ms_chunk_add_constant(&c, MS_INT_VAL(42));
    MsInstruction instr = ms_enc_ABx(MS_OP_LOADK, 0, idx);
    ms_chunk_write(&c, instr, 1, 1);
    TEST_ASSERT_EQ(c.code_count, 1);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_LOADK);
    TEST_ASSERT(MS_AS_INT(c.constants.data[idx]) == 42);
    ms_chunk_free(&c);
}

static void test_rle_lines(void) {
    MsChunk c;
    ms_chunk_init(&c);
    ms_chunk_write(&c, ms_enc_ABx(MS_OP_LOADK, 0, 0), 10, 1);
    ms_chunk_write(&c, ms_enc_ABx(MS_OP_LOADK, 1, 0), 10, 5);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_ADD, 2, 0, 1), 10, 9);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 2, 2, 0), 11, 1);
    TEST_ASSERT_EQ(c.line_count, 4);
    TEST_ASSERT_EQ(ms_chunk_get_line(&c, 0), 10);
    TEST_ASSERT_EQ(ms_chunk_get_line(&c, 2), 10);
    TEST_ASSERT_EQ(ms_chunk_get_line(&c, 3), 11);
    ms_chunk_free(&c);
}

static void test_rle_merge(void) {
    MsChunk c;
    ms_chunk_init(&c);
    /* Same line AND col: all three should merge into one run */
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_NOP, 0, 0, 0), 5, 3);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_NOP, 0, 0, 0), 5, 3);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_NOP, 0, 0, 0), 5, 3);
    TEST_ASSERT_EQ(c.line_count, 1);
    TEST_ASSERT_EQ(ms_chunk_get_line(&c, 1), 5);
    ms_chunk_free(&c);
}

static void test_opcode_name(void) {
    TEST_ASSERT(ms_opcode_name(MS_OP_ADD) != NULL);
    TEST_ASSERT(ms_opcode_name(MS_OP_LOADK) != NULL);
    TEST_ASSERT(ms_opcode_name(MS_OP_NOP) != NULL);
}

int main(void) {
    test_encode_decode_ABC();
    test_encode_decode_AsBx();
    test_chunk_write_and_constants();
    test_rle_lines();
    test_rle_merge();
    test_opcode_name();
    printf("test_chunk: all passed\n");
    return 0;
}
