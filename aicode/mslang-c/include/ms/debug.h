#pragma once
#include "ms/chunk.h"

void ms_disasm_chunk(const MsChunk* chunk, const char* name);
int  ms_disasm_instr(const MsChunk* chunk, int offset);

/* Returns a heap-allocated string containing the disassembly of chunk.
   Caller is responsible for free()-ing the result.
   Returns NULL on allocation failure. */
char* ms_chunk_disassemble_str(const MsChunk* chunk, const char* name);
