#pragma once
#include "ms/chunk.h"

/* Perform peephole optimization on a chunk (in-place).
   Runs 5 passes then compacts NOPs and fixes jump offsets. */
void ms_peephole_optimize(MsChunk* chunk);
