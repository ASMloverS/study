#pragma once
#include "ms/value.h"
#include "ms/opcode.h"

typedef struct {
    int line;
    int column;
    int count;
} MsSourceRun;

typedef struct MsChunk {
    MsInstruction* code;
    int            code_count;
    int            code_capacity;
    MsValueArray   constants;
    MsSourceRun*   lines;
    int            line_count;
    int            line_capacity;
} MsChunk;

void ms_chunk_init(MsChunk* c);
void ms_chunk_free(MsChunk* c);
void ms_chunk_write(MsChunk* c, MsInstruction instr, int line, int col);
int  ms_chunk_add_constant(MsChunk* c, MsValue val);
int  ms_chunk_get_line(MsChunk* c, int offset);
