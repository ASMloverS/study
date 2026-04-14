#include "ms/chunk.h"
#include "ms/memory.h"

void ms_chunk_init(MsChunk* chunk) {
    chunk->code = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->lines = NULL;
    chunk->line_count = 0;
    chunk->line_capacity = 0;
    ms_value_array_init(&chunk->constants);
}

void ms_chunk_free(struct MsVM* vm, MsChunk* chunk) {
    ms_reallocate(vm, chunk->code, (size_t)chunk->capacity * sizeof(ms_u8), 0);
    ms_reallocate(vm, chunk->lines, (size_t)chunk->line_capacity * sizeof(int), 0);
    ms_value_array_free(&chunk->constants);
    ms_chunk_init(chunk);
}
