#pragma once
#include "ms/chunk.h"

void ms_disasm_chunk(const MsChunk* chunk, const char* name);
int  ms_disasm_instr(const MsChunk* chunk, int offset);
