#include "chunk.h"
#include "value.h"
#include <stdio.h>
#include <assert.h>

static void test_chunk_init_free(void) {
	MsChunk chunk;
	ms_chunk_init(&chunk);
	assert(chunk.code == NULL);
	assert(chunk.lines == NULL);
	assert(chunk.count == 0);
	assert(chunk.capacity == 0);
	assert(chunk.constants.values == NULL);
	assert(chunk.constants.count == 0);

	ms_chunk_free(&chunk);
	assert(chunk.code == NULL);
	assert(chunk.lines == NULL);
	assert(chunk.count == 0);
	assert(chunk.capacity == 0);
	printf("  test_chunk_init_free PASSED\n");
}

static void test_chunk_write(void) {
	MsChunk chunk;
	ms_chunk_init(&chunk);

	ms_chunk_write(&chunk, MS_OP_RETURN, 1);
	assert(chunk.count == 1);
	assert(chunk.code[0] == MS_OP_RETURN);
	assert(chunk.lines[0] == 1);

	ms_chunk_write(&chunk, MS_OP_NIL, 2);
	assert(chunk.count == 2);
	assert(chunk.code[1] == MS_OP_NIL);
	assert(chunk.lines[1] == 2);

	ms_chunk_free(&chunk);
	printf("  test_chunk_write PASSED\n");
}

static void test_line_tracking(void) {
	MsChunk chunk;
	ms_chunk_init(&chunk);

	ms_chunk_write(&chunk, MS_OP_NIL, 1);
	ms_chunk_write(&chunk, MS_OP_NIL, 1);
	ms_chunk_write(&chunk, MS_OP_NIL, 2);
	ms_chunk_write(&chunk, MS_OP_RETURN, 3);

	assert(chunk.lines[0] == 1);
	assert(chunk.lines[1] == 1);
	assert(chunk.lines[2] == 2);
	assert(chunk.lines[3] == 3);

	ms_chunk_free(&chunk);
	printf("  test_line_tracking PASSED\n");
}

static void test_chunk_growth(void) {
	MsChunk chunk;
	ms_chunk_init(&chunk);

	for (int i = 0; i < 1000; i++) {
		ms_chunk_write(&chunk, (uint8_t)(i % 256), 1);
	}
	assert(chunk.count == 1000);

	for (int i = 0; i < 1000; i++) {
		assert(chunk.code[i] == (uint8_t)(i % 256));
	}

	ms_chunk_free(&chunk);
	printf("  test_chunk_growth PASSED\n");
}

static void test_constant_pool(void) {
	MsChunk chunk;
	ms_chunk_init(&chunk);

	int idx0 = ms_chunk_add_constant(&chunk, ms_number_val(1.0));
	assert(idx0 == 0);

	int idx1 = ms_chunk_add_constant(&chunk, ms_number_val(2.0));
	assert(idx1 == 1);

	int idx2 = ms_chunk_add_constant(&chunk, ms_number_val(3.0));
	assert(idx2 == 2);

	assert(ms_as_number(chunk.constants.values[0]) == 1.0);
	assert(ms_as_number(chunk.constants.values[1]) == 2.0);
	assert(ms_as_number(chunk.constants.values[2]) == 3.0);

	ms_chunk_write(&chunk, MS_OP_CONSTANT, 1);
	ms_chunk_write(&chunk, (uint8_t)idx0, 1);

	ms_chunk_free(&chunk);
	printf("  test_constant_pool PASSED\n");
}

static void test_disassemble_simple(void) {
	MsChunk chunk;
	ms_chunk_init(&chunk);

	ms_chunk_write(&chunk, MS_OP_NIL, 1);
	ms_chunk_write(&chunk, MS_OP_RETURN, 1);

	int next = ms_chunk_disassemble_instruction(&chunk, 0);
	assert(next == 1);
	next = ms_chunk_disassemble_instruction(&chunk, 1);
	assert(next == 2);

	ms_chunk_disassemble(&chunk, "test");

	ms_chunk_free(&chunk);
	printf("  test_disassemble_simple PASSED\n");
}

static void test_disassemble_constant_and_jump(void) {
	MsChunk chunk;
	ms_chunk_init(&chunk);

	int idx = ms_chunk_add_constant(&chunk, ms_number_val(42.0));
	ms_chunk_write(&chunk, MS_OP_CONSTANT, 1);
	ms_chunk_write(&chunk, (uint8_t)idx, 1);

	ms_chunk_write(&chunk, MS_OP_JUMP, 2);
	ms_chunk_write(&chunk, 0x00, 2);
	ms_chunk_write(&chunk, 0x05, 2);

	ms_chunk_write(&chunk, MS_OP_RETURN, 3);

	int offset = ms_chunk_disassemble_instruction(&chunk, 0);
	assert(offset == 2);

	offset = ms_chunk_disassemble_instruction(&chunk, offset);
	assert(offset == 5);

	offset = ms_chunk_disassemble_instruction(&chunk, offset);
	assert(offset == 6);

	ms_chunk_disassemble(&chunk, "test2");

	ms_chunk_free(&chunk);
	printf("  test_disassemble_constant_and_jump PASSED\n");
}

int main(void) {
	printf("Running chunk tests...\n");
	test_chunk_init_free();
	test_chunk_write();
	test_line_tracking();
	test_chunk_growth();
	test_constant_pool();
	test_disassemble_simple();
	test_disassemble_constant_and_jump();
	printf("All chunk tests passed.\n");
	return 0;
}
