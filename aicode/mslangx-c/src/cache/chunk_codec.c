#include "ms/cache/chunk_codec.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ms/cache/cache_format.h"
#include "ms/string.h"

static int ms_chunk_codec_append_u8(MsBuffer *buffer, uint8_t value) {
  return ms_buffer_append(buffer, &value, sizeof(value));
}

static int ms_chunk_codec_append_u32(MsBuffer *buffer, uint32_t value) {
  uint8_t bytes[MS_CACHE_U32_SIZE];

  if (!ms_cache_write_u32_le(bytes, sizeof(bytes), 0, value)) {
    return 0;
  }
  return ms_buffer_append(buffer, bytes, sizeof(bytes));
}

static int ms_chunk_codec_append_i64(MsBuffer *buffer, int64_t value) {
  uint8_t bytes[MS_CACHE_I64_SIZE];

  if (!ms_cache_write_i64_le(bytes, sizeof(bytes), 0, value)) {
    return 0;
  }
  return ms_buffer_append(buffer, bytes, sizeof(bytes));
}

static int ms_chunk_codec_append_double(MsBuffer *buffer, double value) {
  uint8_t bytes[MS_CACHE_DOUBLE_SIZE];

  if (!ms_cache_write_double_le(bytes, sizeof(bytes), 0, value)) {
    return 0;
  }
  return ms_buffer_append(buffer, bytes, sizeof(bytes));
}

static int ms_chunk_codec_append_string(MsBuffer *buffer,
                                        const MsString *string) {
  uint32_t length;

  if (string == NULL) {
    return 0;
  }
  if (string->length > UINT32_MAX) {
    return 0;
  }

  length = (uint32_t) string->length;
  if (!ms_chunk_codec_append_u32(buffer, length)) {
    return 0;
  }
  if (length == 0) {
    return 1;
  }
  return ms_buffer_append(buffer, string->bytes, string->length);
}

static int ms_chunk_codec_write_constant(MsBuffer *buffer, MsValue value) {
  MsString *string;

  if (ms_value_is_nil(value)) {
    return ms_chunk_codec_append_u8(buffer, MS_CHUNK_CODEC_TAG_NIL);
  }
  if (ms_value_is_bool(value)) {
    int boolean;

    if (!ms_value_get_bool(value, &boolean)) {
      return 0;
    }
    return ms_chunk_codec_append_u8(buffer, MS_CHUNK_CODEC_TAG_BOOL) &&
           ms_chunk_codec_append_u8(buffer, boolean ? 1u : 0u);
  }
  if (ms_value_is_number(value)) {
    double number;

    if (!ms_value_get_number(value, &number)) {
      return 0;
    }
    return ms_chunk_codec_append_u8(buffer, MS_CHUNK_CODEC_TAG_NUMBER) &&
           ms_chunk_codec_append_double(buffer, number);
  }
  if (ms_value_is_string(value)) {
    if (!ms_value_get_string(value, &string)) {
      return 0;
    }
    return ms_chunk_codec_append_u8(buffer, MS_CHUNK_CODEC_TAG_STRING) &&
           ms_chunk_codec_append_string(buffer, string);
  }

  return 0;
}

static int ms_chunk_codec_append_code_and_lines(MsBuffer *buffer,
                                                const MsChunk *chunk) {
  size_t i;

  for (i = 0; i < chunk->code.length; ++i) {
    if (!ms_chunk_codec_append_u8(buffer, chunk->code.data[i])) {
      return 0;
    }
    if (!ms_chunk_codec_append_i64(buffer, (int64_t) chunk->lines[i])) {
      return 0;
    }
  }
  return 1;
}

int ms_chunk_codec_write(const MsChunk *chunk, MsBuffer *out_buffer) {
  MsBuffer payload;
  size_t i;
  uint32_t code_length;
  uint32_t constant_count;
  int ok = 0;

  if (chunk == NULL || out_buffer == NULL) {
    return 0;
  }
  if (chunk->code.length > UINT32_MAX || chunk->constants_count > UINT32_MAX) {
    return 0;
  }
  if (chunk->code.length != 0 && chunk->lines == NULL) {
    return 0;
  }

  ms_buffer_init(&payload);

  code_length = (uint32_t) chunk->code.length;
  constant_count = (uint32_t) chunk->constants_count;

  if (!ms_chunk_codec_append_u32(&payload, code_length)) {
    goto done;
  }
  if (!ms_chunk_codec_append_u32(&payload, code_length)) {
    goto done;
  }
  if (!ms_chunk_codec_append_code_and_lines(&payload, chunk)) {
    goto done;
  }
  if (!ms_chunk_codec_append_u32(&payload, constant_count)) {
    goto done;
  }
  for (i = 0; i < chunk->constants_count; ++i) {
    if (!ms_chunk_codec_write_constant(&payload, chunk->constants[i])) {
      goto done;
    }
  }

  ms_buffer_destroy(out_buffer);
  out_buffer->data = payload.data;
  out_buffer->length = payload.length;
  out_buffer->capacity = payload.capacity;
  payload.data = NULL;
  payload.length = 0;
  payload.capacity = 0;
  ok = 1;

done:
  if (!ok) {
    ms_buffer_destroy(&payload);
    return 0;
  }

  ms_buffer_destroy(&payload);
  return 1;
}

static int ms_chunk_codec_read_u32(const uint8_t *buffer,
                                   size_t buffer_length,
                                   size_t *offset,
                                   uint32_t *out_value) {
  if (!ms_cache_read_u32_le(buffer, buffer_length, *offset, out_value)) {
    return 0;
  }
  *offset += MS_CACHE_U32_SIZE;
  return 1;
}

static int ms_chunk_codec_read_i64(const uint8_t *buffer,
                                   size_t buffer_length,
                                   size_t *offset,
                                   int64_t *out_value) {
  if (!ms_cache_read_i64_le(buffer, buffer_length, *offset, out_value)) {
    return 0;
  }
  *offset += MS_CACHE_I64_SIZE;
  return 1;
}

static int ms_chunk_codec_read_double(const uint8_t *buffer,
                                      size_t buffer_length,
                                      size_t *offset,
                                      double *out_value) {
  if (!ms_cache_read_double_le(buffer, buffer_length, *offset, out_value)) {
    return 0;
  }
  *offset += MS_CACHE_DOUBLE_SIZE;
  return 1;
}

static int ms_chunk_codec_read_string(const uint8_t *buffer,
                                      size_t buffer_length,
                                      size_t *offset,
                                      MsString **out_string) {
  uint32_t length;
  MsString *string;

  if (!ms_chunk_codec_read_u32(buffer, buffer_length, offset, &length)) {
    return 0;
  }
  if (length > buffer_length - *offset) {
    return 0;
  }

  string = ms_string_new(length == 0 ? NULL : (const char *) (buffer + *offset),
                         length);
  if (string == NULL) {
    return 0;
  }

  *offset += length;
  *out_string = string;
  return 1;
}

static void ms_chunk_codec_free_read_strings(MsChunk *chunk) {
  size_t i;
  MsString *string;

  if (chunk == NULL) {
    return;
  }

  for (i = 0; i < chunk->constants_count; ++i) {
    if (ms_value_is_string(chunk->constants[i]) &&
        ms_value_get_string(chunk->constants[i], &string)) {
      ms_string_free(string);
    }
  }
}

static int ms_chunk_codec_read_constant(const uint8_t *buffer,
                                        size_t buffer_length,
                                        size_t *offset,
                                        MsChunk *chunk) {
  uint8_t tag;
  uint8_t boolean;
  double number;
  MsString *string;
  MsValue value;
  uint8_t index;

  if (buffer == NULL || offset == NULL || chunk == NULL) {
    return 0;
  }
  if (*offset >= buffer_length) {
    return 0;
  }

  tag = buffer[*offset];
  *offset += 1;

  switch (tag) {
    case MS_CHUNK_CODEC_TAG_NIL:
      value = ms_value_nil();
      break;
    case MS_CHUNK_CODEC_TAG_BOOL:
      if (*offset >= buffer_length) {
        return 0;
      }
      boolean = buffer[*offset];
      if (boolean != 0 && boolean != 1) {
        return 0;
      }
      *offset += 1;
      value = ms_value_bool(boolean);
      break;
    case MS_CHUNK_CODEC_TAG_NUMBER:
      if (!ms_chunk_codec_read_double(buffer, buffer_length, offset, &number)) {
        return 0;
      }
      value = ms_value_number(number);
      break;
    case MS_CHUNK_CODEC_TAG_STRING:
      if (!ms_chunk_codec_read_string(buffer, buffer_length, offset, &string)) {
        return 0;
      }
      value = ms_value_object((MsObject *) string);
      break;
    default:
      return 0;
  }

  if (!ms_chunk_add_constant(chunk, value, &index)) {
    if (ms_value_is_string(value) && ms_value_get_string(value, &string)) {
      ms_string_free(string);
    }
    return 0;
  }

  return 1;
}

int ms_chunk_codec_read(const uint8_t *buffer,
                        size_t buffer_length,
                        MsChunk *out_chunk) {
  uint32_t code_length;
  uint32_t line_count;
  uint32_t constant_count;
  size_t offset = 0;
  size_t i;
  int64_t line_value;
  uint8_t byte;
  int ok = 0;

  if (buffer == NULL || out_chunk == NULL) {
    return 0;
  }

  ms_chunk_destroy(out_chunk);
  ms_chunk_init(out_chunk);

  if (!ms_chunk_codec_read_u32(buffer, buffer_length, &offset, &code_length)) {
    goto done;
  }
  if ((size_t) code_length > buffer_length - offset) {
    goto done;
  }
  if (!ms_chunk_codec_read_u32(buffer, buffer_length, &offset, &line_count)) {
    goto done;
  }
  if (line_count != code_length) {
    goto done;
  }

  for (i = 0; i < code_length; ++i) {
    if (offset >= buffer_length) {
      goto done;
    }
    byte = buffer[offset];
    offset += 1;
    if (!ms_chunk_codec_read_i64(buffer, buffer_length, &offset, &line_value)) {
      goto done;
    }
    if (line_value < INT_MIN || line_value > INT_MAX) {
      goto done;
    }
    if (!ms_chunk_write(out_chunk, byte, (int) line_value)) {
      goto done;
    }
  }

  if (!ms_chunk_codec_read_u32(buffer, buffer_length, &offset, &constant_count)) {
    goto done;
  }

  for (i = 0; i < constant_count; ++i) {
    if (!ms_chunk_codec_read_constant(buffer, buffer_length, &offset, out_chunk)) {
      goto done;
    }
  }

  ok = offset == buffer_length;

done:
  if (!ok) {
    ms_chunk_codec_free_read_strings(out_chunk);
    ms_chunk_destroy(out_chunk);
    ms_chunk_init(out_chunk);
    return 0;
  }

  return 1;
}
