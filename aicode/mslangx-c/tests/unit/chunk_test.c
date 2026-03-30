#include <stdint.h>

#include "ms/runtime/chunk.h"
#include "ms/runtime/opcode.h"
#include "ms/value.h"

#include "test_assert.h"

int main(void) {
  MsChunk chunk;
  uint8_t first_constant = 0;
  uint8_t second_constant = 0;
  MsValue stored = ms_value_nil();
  uint16_t jump_operand = 0;
  int line = 0;

  ms_chunk_init(&chunk);

  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_number(1.25),
                                    &first_constant));
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_number(7.0),
                                    &second_constant));
  TEST_ASSERT(first_constant == 0);
  TEST_ASSERT(second_constant == 1);

  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 3));
  TEST_ASSERT(ms_chunk_write(&chunk, first_constant, 3));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_JUMP, 4));
  TEST_ASSERT(ms_chunk_write_short(&chunk, 0xffffu, 4));
  TEST_ASSERT(ms_chunk_patch_short(&chunk, 3, 0x0012u));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 5));
  TEST_ASSERT(ms_chunk_write(&chunk, second_constant, 5));

  TEST_ASSERT(ms_chunk_constant_count(&chunk) == 2);
  TEST_ASSERT(ms_chunk_code_count(&chunk) == 7);

  TEST_ASSERT(ms_chunk_get_constant(&chunk, 0, &stored));
  TEST_ASSERT(ms_value_equals(stored, ms_value_number(1.25)));
  TEST_ASSERT(ms_chunk_get_constant(&chunk, 1, &stored));
  TEST_ASSERT(ms_value_equals(stored, ms_value_number(7.0)));

  TEST_ASSERT(ms_chunk_get_line(&chunk, 0, &line));
  TEST_ASSERT(line == 3);
  TEST_ASSERT(ms_chunk_get_line(&chunk, 1, &line));
  TEST_ASSERT(line == 3);
  TEST_ASSERT(ms_chunk_get_line(&chunk, 2, &line));
  TEST_ASSERT(line == 4);
  TEST_ASSERT(ms_chunk_get_line(&chunk, 3, &line));
  TEST_ASSERT(line == 4);
  TEST_ASSERT(ms_chunk_get_line(&chunk, 5, &line));
  TEST_ASSERT(line == 5);

  TEST_ASSERT(ms_chunk_read_short(&chunk, 3, &jump_operand));
  TEST_ASSERT(jump_operand == 0x0012u);

  ms_chunk_destroy(&chunk);
  return 0;
}