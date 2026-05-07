#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ms/buffer.h"
#include "ms/cache/chunk_codec.h"
#include "ms/frontend/resolution_table.h"
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

static int expect_function_value(MsValue value,
                                 const char *expected_name,
                                 int expected_arity,
                                 uint8_t expected_upvalue_count,
                                 unsigned expected_flags,
                                 MsFunction **out_function) {
  MsFunction *function;

  TEST_ASSERT(ms_value_is_function(value));
  TEST_ASSERT(ms_value_get_function(value, &function));
  TEST_ASSERT(function != NULL);
  if (expected_name == NULL) {
    TEST_ASSERT(function->name == NULL);
  } else {
    TEST_ASSERT(function->name != NULL);
    TEST_ASSERT(strcmp(function->name->bytes, expected_name) == 0);
  }
  TEST_ASSERT(function->arity == expected_arity);
  TEST_ASSERT(function->upvalue_count == expected_upvalue_count);
  TEST_ASSERT(function->flags == expected_flags);
  if (out_function != NULL) {
    *out_function = function;
  }
  return 0;
}

static int free_chunk_constants_recursive(MsChunk *chunk) {
  size_t i;
  MsString *string;
  MsFunction *function;

  if (chunk == NULL) {
    return 0;
  }

  for (i = 0; i < chunk->constants_count; ++i) {
    if (ms_value_is_string(chunk->constants[i]) &&
        ms_value_get_string(chunk->constants[i], &string)) {
      ms_string_free(string);
    } else if (ms_value_is_function(chunk->constants[i]) &&
               ms_value_get_function(chunk->constants[i], &function)) {
      free_chunk_constants_recursive(&function->chunk);
      ms_function_free(function);
    }
  }

  return 0;
}

static void free_self_referential_function(MsFunction *function) {
  MsValue constant;
  MsFunction *nested_function;

  if (function == NULL) {
    return;
  }

  if (ms_chunk_constant_count(&function->chunk) == 1 &&
      ms_chunk_get_constant(&function->chunk, 0, &constant) &&
      ms_value_is_function(constant) &&
      ms_value_get_function(constant, &nested_function) &&
      nested_function == function) {
    function->chunk.constants[0] = ms_value_nil();
  }

  ms_function_free(function);
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

  free_chunk_constants_recursive(&decoded);
  ms_chunk_destroy(&decoded);
  ms_buffer_destroy(&payload);
  free_chunk_constants_recursive(&chunk);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_function_constant_round_trip(void) {
  MsChunk chunk;
  MsChunk decoded;
  MsBuffer payload;
  MsFunction *outer;
  MsFunction *inner;
  MsFunction *decoded_outer;
  MsFunction *decoded_inner;
  MsString *inner_note;
  MsValue stored = ms_value_nil();
  uint8_t outer_index = 0;
  uint8_t inner_index = 0;

  ms_chunk_init(&chunk);
  ms_chunk_init(&decoded);
  ms_buffer_init(&payload);

  outer = ms_function_new("outer", strlen("outer"), 2);
  inner = ms_function_new(NULL, 0, 1);
  inner_note = ms_string_from_cstr("inner-note");
  TEST_ASSERT(outer != NULL);
  TEST_ASSERT(inner != NULL);
  TEST_ASSERT(inner_note != NULL);

  outer->upvalue_count = 1;
  outer->flags = MS_FUNCTION_FLAG_METHOD | MS_FUNCTION_FLAG_HAS_SELF;
  TEST_ASSERT(ms_chunk_write(&outer->chunk, 0x91u, 12));
  TEST_ASSERT(ms_chunk_add_constant(&outer->chunk, ms_value_number(99.5), &inner_index));
  TEST_ASSERT(inner_index == 0);

  inner->upvalue_count = 0;
  inner->flags = MS_FUNCTION_FLAG_INITIALIZER;
  TEST_ASSERT(ms_chunk_write(&inner->chunk, 0x44u, 27));
  TEST_ASSERT(ms_chunk_add_constant(&inner->chunk,
                                    ms_value_object((MsObject *) inner_note),
                                    &inner_index));
  TEST_ASSERT(inner_index == 0);

  TEST_ASSERT(ms_chunk_add_constant(&outer->chunk,
                                    ms_value_object((MsObject *) inner),
                                    &outer_index));
  TEST_ASSERT(outer_index == 1);
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_object((MsObject *) outer),
                                    &outer_index));
  TEST_ASSERT(outer_index == 0);

  TEST_ASSERT(ms_chunk_codec_write(&chunk, &payload));
  TEST_ASSERT(ms_chunk_codec_read(payload.data, payload.length, &decoded));
  TEST_ASSERT(ms_chunk_constant_count(&decoded) == 1);
  TEST_ASSERT(ms_chunk_get_constant(&decoded, 0, &stored));
  TEST_ASSERT(expect_function_value(stored,
                                   "outer",
                                   2,
                                   1,
                                   MS_FUNCTION_FLAG_METHOD | MS_FUNCTION_FLAG_HAS_SELF,
                                   &decoded_outer) == 0);
  TEST_ASSERT(ms_chunk_code_count(&decoded_outer->chunk) == 1);
  TEST_ASSERT(decoded_outer->chunk.code.data[0] == 0x91u);
  TEST_ASSERT(decoded_outer->chunk.lines[0] == 12);
  TEST_ASSERT(ms_chunk_constant_count(&decoded_outer->chunk) == 2);

  TEST_ASSERT(ms_chunk_get_constant(&decoded_outer->chunk, 0, &stored));
  TEST_ASSERT(ms_value_is_number(stored));
  TEST_ASSERT(ms_value_equals(stored, ms_value_number(99.5)));

  TEST_ASSERT(ms_chunk_get_constant(&decoded_outer->chunk, 1, &stored));
  TEST_ASSERT(expect_function_value(stored,
                                     NULL,
                                     1,
                                     0,
                                     MS_FUNCTION_FLAG_INITIALIZER,
                                     &decoded_inner) == 0);
  TEST_ASSERT(ms_chunk_code_count(&decoded_inner->chunk) == 1);
  TEST_ASSERT(decoded_inner->chunk.code.data[0] == 0x44u);
  TEST_ASSERT(decoded_inner->chunk.lines[0] == 27);
  TEST_ASSERT(ms_chunk_constant_count(&decoded_inner->chunk) == 1);
  TEST_ASSERT(ms_chunk_get_constant(&decoded_inner->chunk, 0, &stored));
  TEST_ASSERT(expect_string_value(stored, "inner-note") == 0);

  TEST_ASSERT(decoded_outer != outer);
  TEST_ASSERT(decoded_inner != inner);
  TEST_ASSERT(decoded_outer->chunk.code.data != outer->chunk.code.data);
  TEST_ASSERT(decoded_inner->chunk.code.data != inner->chunk.code.data);

  free_chunk_constants_recursive(&decoded);
  ms_chunk_destroy(&decoded);
  ms_buffer_destroy(&payload);
  free_chunk_constants_recursive(&chunk);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_writer_rejects_unsupported_constants(void) {
  MsChunk chunk;
  MsBuffer payload;
  MsClass *klass;
  uint8_t index = 0;

  ms_chunk_init(&chunk);
  ms_buffer_init(&payload);

  klass = ms_class_new("nested", strlen("nested"), NULL);
  TEST_ASSERT(klass != NULL);
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_object((MsObject *) klass),
                                    &index));
  TEST_ASSERT(index == 0);
  TEST_ASSERT(!ms_chunk_codec_write(&chunk, &payload));
  TEST_ASSERT(payload.length == 0);

  ms_class_free(klass);
  ms_buffer_destroy(&payload);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_writer_rejects_recursive_function_graph(void) {
  MsChunk chunk;
  MsBuffer payload;
  MsFunction *recursive;
  uint8_t index = 0;
  const unsigned char sentinel[] = {0xaa, 0xbb, 0xcc};

  ms_chunk_init(&chunk);
  ms_buffer_init(&payload);

  recursive = ms_function_new("self", strlen("self"), 0);
  TEST_ASSERT(recursive != NULL);

  recursive->upvalue_count = 0;
  recursive->flags = 0;
  TEST_ASSERT(ms_chunk_add_constant(&recursive->chunk,
                                    ms_value_object((MsObject *) recursive),
                                    &index));
  TEST_ASSERT(index == 0);
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_object((MsObject *) recursive),
                                    &index));
  TEST_ASSERT(index == 0);

  TEST_ASSERT(ms_buffer_append(&payload, sentinel, sizeof(sentinel)));
  TEST_ASSERT(!ms_chunk_codec_write(&chunk, &payload));
  TEST_ASSERT(payload.length == sizeof(sentinel));
  TEST_ASSERT(memcmp(payload.data, sentinel, sizeof(sentinel)) == 0);

  free_self_referential_function(recursive);
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

static int test_reader_rejects_truncated_function_payload(void) {
  uint8_t payload[] = {
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x01,
      0x04,
      0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00,
      0x00, 0x00, 0x00, 0x00,
      0x0c, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00};
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
  TEST_ASSERT(test_function_constant_round_trip() == 0);
  TEST_ASSERT(test_writer_rejects_unsupported_constants() == 0);
  TEST_ASSERT(test_writer_rejects_recursive_function_graph() == 0);
  TEST_ASSERT(test_writer_overwrites_existing_buffer() == 0);
  TEST_ASSERT(test_reader_rejects_unknown_tag() == 0);
  TEST_ASSERT(test_reader_rejects_truncated_function_payload() == 0);
  return 0;
}
