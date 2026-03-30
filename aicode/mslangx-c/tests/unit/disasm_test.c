#include <string.h>

#include "ms/buffer.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/opcode.h"
#include "ms/value.h"

#include "test_assert.h"

int main(void) {
  MsChunk chunk;
  MsBuffer output;
  uint8_t first_constant = 0;
  uint8_t second_constant = 0;
  static const char kExpected[] =
      "== sample ==\n"
      "0000    1 OP_CONSTANT      0 '1'\n"
      "0002    | OP_CONSTANT      1 '2'\n"
      "0004    | OP_ADD\n"
      "0005    2 OP_RETURN\n";

  ms_chunk_init(&chunk);
  ms_buffer_init(&output);

  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_number(1.0),
                                    &first_constant));
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_number(2.0),
                                    &second_constant));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 1));
  TEST_ASSERT(ms_chunk_write(&chunk, first_constant, 1));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 1));
  TEST_ASSERT(ms_chunk_write(&chunk, second_constant, 1));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_ADD, 1));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_RETURN, 2));

  TEST_ASSERT(ms_chunk_disassemble_to_buffer(&chunk, "sample", &output));
  TEST_ASSERT(output.length == strlen(kExpected));
  TEST_ASSERT(memcmp(output.data, kExpected, output.length) == 0);

  ms_buffer_destroy(&output);
  ms_chunk_destroy(&chunk);
  return 0;
}