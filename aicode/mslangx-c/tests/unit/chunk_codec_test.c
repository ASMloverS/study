#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ms/buffer.h"
#include "ms/cache/chunk_codec.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/function.h"
#include "ms/value.h"

#include "test_assert.h"

static int expect_string_value(MsValue value, const char *expected) {
  MsString *string;

  TEST_ASSERT(ms_value_is_string(value));
  TEST_ASSERT(ms_value_get_string(value, &string));
  TEST_ASSERT(string != NULL);
  TEST_ASSERT(strcmp(string->bytes, expected) == 0);
  return 0;
}

static int free_string_constants(MsChunk *chunk) {
  size_t i;
  MsString *string;

  for (i = 0; i < chunk->constants_count; ++i) {
    if (ms_value_is_string(chunk->constants[i]) &&
        ms_value_get_string(chunk->constants[i], &string)) {
      ms_string_free(string);
    }
  }

  return 0;
}

static int test_empty_chunk_round_trip(void) {
  MsChunk chunk;
  MsChunk decoded;
  MsBuffer payload;

  ms_chunk_init(&chunk);
  ms_chunk_init(&decoded);
  ms_buffer_init(&payload);

  TEST_ASSERT(ms_chunk_codec_write(&chunk, &payload));
  TEST_ASSERT(payload.length == 12);
  TEST_ASSERT(ms_chunk_codec_read(payload.data, payload.length, &decoded));
  TEST_ASSERT(ms_chunk_code_count(&decoded) == 0);
  TEST_ASSERT(ms_chunk_constant_count(&decoded) == 0);

  ms_chunk_destroy(&decoded);
  ms_buffer_destroy(&payload);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_code_and_lines_round_trip(void) {
  MsChunk chunk;
  MsChunk decoded;
  MsBuffer payload;

  ms_chunk_init(&chunk);
  ms_chunk_init(&decoded);
  ms_buffer_init(&payload);

  TEST_ASSERT(ms_chunk_write(&chunk, 0x11u, 7));
  TEST_ASSERT(ms_chunk_write(&chunk, 0x22u, 7));
  TEST_ASSERT(ms_chunk_write(&chunk, 0x33u, 8));

  TEST_ASSERT(ms_chunk_codec_write(&chunk, &payload));
  TEST_ASSERT(ms_chunk_codec_read(payload.data, payload.length, &decoded));
  TEST_ASSERT(ms_chunk_code_count(&decoded) == 3);
  TEST_ASSERT(decoded.code.data[0] == 0x11u);
  TEST_ASSERT(decoded.code.data[1] == 0x22u);
  TEST_ASSERT(decoded.code.data[2] == 0x33u);
  TEST_ASSERT(decoded.lines[0] == 7);
  TEST_ASSERT(decoded.lines[1] == 7);
  TEST_ASSERT(decoded.lines[2] == 8);
  TEST_ASSERT(decoded.code.data != chunk.code.data);
  TEST_ASSERT(decoded.lines != chunk.lines);

  ms_chunk_destroy(&decoded);
  ms_buffer_destroy(&payload);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_constants_round_trip(void) {
  MsChunk chunk;
  MsChunk decoded;
  MsBuffer payload;
  MsValue stored = ms_value_nil();
  uint8_t index = 0;
  uint8_t decoded_index = 0;
  MsString *source_string;
  MsString *decoded_string;

  ms_chunk_init(&chunk);
  ms_chunk_init(&decoded);
  ms_buffer_init(&payload);

  TEST_ASSERT(ms_chunk_add_constant(&chunk, ms_value_nil(), &index));
  TEST_ASSERT(index == 0);
  TEST_ASSERT(ms_chunk_add_constant(&chunk, ms_value_bool(1), &index));
  TEST_ASSERT(index == 1);
  TEST_ASSERT(ms_chunk_add_constant(&chunk, ms_value_number(12.5), &index));
  TEST_ASSERT(index == 2);
  source_string = ms_string_from_cstr("hello");
  TEST_ASSERT(source_string != NULL);
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_object((MsObject *) source_string),
                                    &index));
  TEST_ASSERT(index == 3);

  TEST_ASSERT(ms_chunk_codec_write(&chunk, &payload));
  ms_string_free(source_string);
  TEST_ASSERT(ms_chunk_codec_read(payload.data, payload.length, &decoded));
  TEST_ASSERT(ms_chunk_constant_count(&decoded) == 4);

  TEST_ASSERT(ms_chunk_get_constant(&decoded, 0, &stored));
  TEST_ASSERT(ms_value_is_nil(stored));

  TEST_ASSERT(ms_chunk_get_constant(&decoded, 1, &stored));
  TEST_ASSERT(ms_value_is_bool(stored));
  TEST_ASSERT(ms_value_equals(stored, ms_value_bool(1)));

  TEST_ASSERT(ms_chunk_get_constant(&decoded, 2, &stored));
  TEST_ASSERT(ms_value_is_number(stored));
  TEST_ASSERT(ms_value_equals(stored, ms_value_number(12.5)));

  TEST_ASSERT(ms_chunk_get_constant(&decoded, 3, &stored));
  TEST_ASSERT(expect_string_value(stored, "hello") == 0);
  TEST_ASSERT(ms_value_get_string(stored, &decoded_string));
  TEST_ASSERT(decoded_string != NULL);
  TEST_ASSERT(decoded_string->bytes != NULL);

  TEST_ASSERT(ms_chunk_add_constant(&decoded, ms_value_nil(), &decoded_index));
  TEST_ASSERT(decoded_index == 4);

  free_string_constants(&decoded);
  ms_chunk_destroy(&decoded);
  ms_buffer_destroy(&payload);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_writer_rejects_unsupported_constants(void) {
  MsChunk chunk;
  MsBuffer payload;
  MsFunction *function;
  uint8_t index = 0;

  ms_chunk_init(&chunk);
  ms_buffer_init(&payload);

  function = ms_function_new("nested", strlen("nested"), 0);
  TEST_ASSERT(function != NULL);
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_object((MsObject *) function),
                                    &index));
  TEST_ASSERT(index == 0);
  TEST_ASSERT(!ms_chunk_codec_write(&chunk, &payload));
  TEST_ASSERT(payload.length == 0);

  ms_function_free(function);
  ms_buffer_destroy(&payload);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_writer_overwrites_existing_buffer(void) {
  MsChunk chunk;
  MsBuffer payload;
  const unsigned char sentinel[] = {0xaa, 0xbb, 0xcc};

  ms_chunk_init(&chunk);
  ms_buffer_init(&payload);

  TEST_ASSERT(ms_buffer_append(&payload, sentinel, sizeof(sentinel)));
  TEST_ASSERT(ms_chunk_codec_write(&chunk, &payload));
  TEST_ASSERT(payload.length == 12);
  TEST_ASSERT(payload.capacity >= payload.length);
  TEST_ASSERT(payload.data[0] == 0x00u);
  TEST_ASSERT(payload.data[1] == 0x00u);
  TEST_ASSERT(payload.data[2] == 0x00u);

  ms_buffer_destroy(&payload);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_reader_rejects_unknown_tag(void) {
  uint8_t payload[] = {
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x01,
      0xff,
      0x00, 0x00, 0x00, 0x00};
  MsChunk chunk;

  ms_chunk_init(&chunk);

  TEST_ASSERT(!ms_chunk_codec_read(payload, sizeof(payload), &chunk));
  TEST_ASSERT(ms_chunk_code_count(&chunk) == 0);
  TEST_ASSERT(ms_chunk_constant_count(&chunk) == 0);

  ms_chunk_destroy(&chunk);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_empty_chunk_round_trip() == 0);
  TEST_ASSERT(test_code_and_lines_round_trip() == 0);
  TEST_ASSERT(test_constants_round_trip() == 0);
  TEST_ASSERT(test_writer_rejects_unsupported_constants() == 0);
  TEST_ASSERT(test_writer_overwrites_existing_buffer() == 0);
  TEST_ASSERT(test_reader_rejects_unknown_tag() == 0);
  return 0;
}
