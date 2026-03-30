#ifndef MSLANGC_RUNTIME_CHUNK_H_
#define MSLANGC_RUNTIME_CHUNK_H_

#include <stddef.h>
#include <stdint.h>

#include "ms/buffer.h"
#include "ms/value.h"

typedef struct MsChunk {
  MsBuffer code;
  int *lines;
  size_t lines_capacity;
  MsValue *constants;
  size_t constants_count;
  size_t constants_capacity;
} MsChunk;

void ms_chunk_init(MsChunk *chunk);
void ms_chunk_destroy(MsChunk *chunk);

size_t ms_chunk_code_count(const MsChunk *chunk);
size_t ms_chunk_constant_count(const MsChunk *chunk);

int ms_chunk_write(MsChunk *chunk, uint8_t byte, int line);
int ms_chunk_write_short(MsChunk *chunk, uint16_t value, int line);
int ms_chunk_patch_byte(MsChunk *chunk, size_t offset, uint8_t byte);
int ms_chunk_patch_short(MsChunk *chunk, size_t offset, uint16_t value);
int ms_chunk_read_byte(const MsChunk *chunk, size_t offset, uint8_t *out_byte);
int ms_chunk_read_short(const MsChunk *chunk, size_t offset, uint16_t *out_value);

int ms_chunk_add_constant(MsChunk *chunk, MsValue value, uint8_t *out_index);
int ms_chunk_get_constant(const MsChunk *chunk, size_t index, MsValue *out_value);
int ms_chunk_get_line(const MsChunk *chunk, size_t offset, int *out_line);

int ms_chunk_disassemble_to_buffer(const MsChunk *chunk,
                                   const char *name,
                                   MsBuffer *buffer);

#endif
