#include "ms/runtime/chunk.h"

#include <limits.h>
#include <stdlib.h>

static int ms_chunk_ensure_lines(MsChunk *chunk, size_t min_capacity) {
  int *lines;
  size_t new_capacity;

  if (chunk == NULL) {
    return 0;
  }
  if (min_capacity <= chunk->lines_capacity) {
    return 1;
  }

  new_capacity = chunk->lines_capacity == 0 ? 16 : chunk->lines_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  lines = (int *) realloc(chunk->lines, new_capacity * sizeof(*lines));
  if (lines == NULL) {
    return 0;
  }

  chunk->lines = lines;
  chunk->lines_capacity = new_capacity;
  return 1;
}

static int ms_chunk_ensure_constants(MsChunk *chunk, size_t min_capacity) {
  MsValue *constants;
  size_t new_capacity;

  if (chunk == NULL) {
    return 0;
  }
  if (min_capacity <= chunk->constants_capacity) {
    return 1;
  }

  new_capacity = chunk->constants_capacity == 0 ? 8 : chunk->constants_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  constants = (MsValue *) realloc(chunk->constants,
                                  new_capacity * sizeof(*constants));
  if (constants == NULL) {
    return 0;
  }

  chunk->constants = constants;
  chunk->constants_capacity = new_capacity;
  return 1;
}

void ms_chunk_init(MsChunk *chunk) {
  if (chunk == NULL) {
    return;
  }

  ms_buffer_init(&chunk->code);
  chunk->lines = NULL;
  chunk->lines_capacity = 0;
  chunk->constants = NULL;
  chunk->constants_count = 0;
  chunk->constants_capacity = 0;
}

void ms_chunk_destroy(MsChunk *chunk) {
  if (chunk == NULL) {
    return;
  }

  ms_buffer_destroy(&chunk->code);
  free(chunk->lines);
  free(chunk->constants);
  chunk->lines = NULL;
  chunk->lines_capacity = 0;
  chunk->constants = NULL;
  chunk->constants_count = 0;
  chunk->constants_capacity = 0;
}

size_t ms_chunk_code_count(const MsChunk *chunk) {
  return chunk == NULL ? 0 : chunk->code.length;
}

size_t ms_chunk_constant_count(const MsChunk *chunk) {
  return chunk == NULL ? 0 : chunk->constants_count;
}

int ms_chunk_write(MsChunk *chunk, uint8_t byte, int line) {
  size_t offset;

  if (chunk == NULL) {
    return 0;
  }

  offset = chunk->code.length;
  if (!ms_chunk_ensure_lines(chunk, offset + 1)) {
    return 0;
  }
  if (!ms_buffer_append(&chunk->code, &byte, sizeof(byte))) {
    return 0;
  }

  chunk->lines[offset] = line;
  return 1;
}

int ms_chunk_write_short(MsChunk *chunk, uint16_t value, int line) {
  return ms_chunk_write(chunk, (uint8_t) ((value >> 8) & 0xffu), line) &&
         ms_chunk_write(chunk, (uint8_t) (value & 0xffu), line);
}

int ms_chunk_patch_byte(MsChunk *chunk, size_t offset, uint8_t byte) {
  if (chunk == NULL || offset >= chunk->code.length) {
    return 0;
  }

  return ms_buffer_write(&chunk->code, offset, &byte, sizeof(byte));
}

int ms_chunk_patch_short(MsChunk *chunk, size_t offset, uint16_t value) {
  uint8_t bytes[2];

  if (chunk == NULL || offset + 1 >= chunk->code.length) {
    return 0;
  }

  bytes[0] = (uint8_t) ((value >> 8) & 0xffu);
  bytes[1] = (uint8_t) (value & 0xffu);
  return ms_buffer_write(&chunk->code, offset, bytes, sizeof(bytes));
}

int ms_chunk_read_byte(const MsChunk *chunk, size_t offset, uint8_t *out_byte) {
  if (chunk == NULL || out_byte == NULL || offset >= chunk->code.length) {
    return 0;
  }

  *out_byte = chunk->code.data[offset];
  return 1;
}

int ms_chunk_read_short(const MsChunk *chunk, size_t offset, uint16_t *out_value) {
  if (chunk == NULL || out_value == NULL || offset + 1 >= chunk->code.length) {
    return 0;
  }

  *out_value = (uint16_t) ((uint16_t) chunk->code.data[offset] << 8);
  *out_value |= (uint16_t) chunk->code.data[offset + 1];
  return 1;
}

int ms_chunk_add_constant(MsChunk *chunk, MsValue value, uint8_t *out_index) {
  size_t index;

  if (chunk == NULL || out_index == NULL) {
    return 0;
  }

  if (chunk->constants_count >= ((size_t) UCHAR_MAX + 1)) {
    return 0;
  }
  if (!ms_chunk_ensure_constants(chunk, chunk->constants_count + 1)) {
    return 0;
  }

  index = chunk->constants_count;
  chunk->constants[index] = value;
  chunk->constants_count += 1;
  *out_index = (uint8_t) index;
  return 1;
}

int ms_chunk_get_constant(const MsChunk *chunk, size_t index, MsValue *out_value) {
  if (chunk == NULL || out_value == NULL || index >= chunk->constants_count) {
    return 0;
  }

  *out_value = chunk->constants[index];
  return 1;
}

int ms_chunk_get_line(const MsChunk *chunk, size_t offset, int *out_line) {
  if (chunk == NULL || out_line == NULL || offset >= chunk->code.length) {
    return 0;
  }

  *out_line = chunk->lines[offset];
  return 1;
}