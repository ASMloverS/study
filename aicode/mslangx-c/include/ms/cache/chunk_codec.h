#ifndef MSLANGC_CACHE_CHUNK_CODEC_H_
#define MSLANGC_CACHE_CHUNK_CODEC_H_

#include <stddef.h>
#include <stdint.h>

#include "ms/buffer.h"
#include "ms/runtime/chunk.h"

typedef enum MsChunkCodecTag {
  MS_CHUNK_CODEC_TAG_NIL = 0,
  MS_CHUNK_CODEC_TAG_BOOL = 1,
  MS_CHUNK_CODEC_TAG_NUMBER = 2,
  MS_CHUNK_CODEC_TAG_STRING = 3,
  MS_CHUNK_CODEC_TAG_FUNCTION = 4
} MsChunkCodecTag;

int ms_chunk_codec_write(const MsChunk *chunk, MsBuffer *out_buffer);
int ms_chunk_codec_read(const uint8_t *buffer,
                        size_t buffer_length,
                        MsChunk *out_chunk);

#endif
