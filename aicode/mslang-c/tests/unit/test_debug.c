#include "test_assert.h"
#include "ms/debug.h"

static void test_disasm_basic(void) {
    MsChunk c;
    ms_chunk_init(&c);
    int k = ms_chunk_add_constant(&c, MS_INT_VAL(42));
    ms_chunk_write(&c, ms_enc_ABx(MS_OP_LOADK, 0, k), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_ADD, 2, 0, 1), 1, 5);
    ms_chunk_write(&c, ms_enc_AsBx(MS_OP_JMP, 0, -2), 2, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0), 2, 5);
    ms_disasm_chunk(&c, "test");
    ms_chunk_free(&c);
}

static void test_disasm_offset_return(void) {
    MsChunk c;
    ms_chunk_init(&c);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_LOADTRUE, 0, 0, 0), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 2, 0), 1, 5);
    int next = ms_disasm_instr(&c, 0);
    TEST_ASSERT_EQ(next, 1);
    next = ms_disasm_instr(&c, 1);
    TEST_ASSERT_EQ(next, 2);
    ms_chunk_free(&c);
}

int main(void) {
    test_disasm_basic();
    test_disasm_offset_return();
    printf("test_debug: all passed\n");
    return 0;
}
