#pragma once
#include "ms/value.h"

// Stub for T05. Full impl in T06.
typedef struct {
    ms_u8*       code;
    int          count;
    int          capacity;
    MsValueArray constants;
    int*         lines;
    int          line_count;
    int          line_capacity;
} MsChunk;

struct MsVM;

void ms_chunk_init(MsChunk* chunk);
void ms_chunk_free(struct MsVM* vm, MsChunk* chunk);
