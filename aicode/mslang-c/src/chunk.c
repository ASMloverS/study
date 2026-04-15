#include "ms/chunk.h"
#include <stdlib.h>

void ms_chunk_init(MsChunk* c) {
    c->code          = NULL;
    c->code_count    = 0;
    c->code_capacity = 0;
    c->lines         = NULL;
    c->line_count    = 0;
    c->line_capacity = 0;
    ms_value_array_init(&c->constants);
}

void ms_chunk_free(MsChunk* c) {
    free(c->code);
    free(c->lines);
    ms_value_array_free(&c->constants);
    ms_chunk_init(c);
}

void ms_chunk_write(MsChunk* c, MsInstruction instr, int line, int col) {
    if (c->code_count >= c->code_capacity) {
        int cap = c->code_capacity < 8 ? 8 : c->code_capacity * 2;
        c->code = (MsInstruction*)realloc(c->code, sizeof(MsInstruction) * (size_t)cap);
        if (!c->code) abort();
        c->code_capacity = cap;
    }
    c->code[c->code_count++] = instr;

    /* RLE: merge if last run has same line AND col */
    if (c->line_count > 0) {
        MsSourceRun* last = &c->lines[c->line_count - 1];
        if (last->line == line && last->column == col) {
            last->count++;
            return;
        }
    }
    if (c->line_count >= c->line_capacity) {
        int cap = c->line_capacity < 8 ? 8 : c->line_capacity * 2;
        c->lines = (MsSourceRun*)realloc(c->lines, sizeof(MsSourceRun) * (size_t)cap);
        if (!c->lines) abort();
        c->line_capacity = cap;
    }
    c->lines[c->line_count++] = (MsSourceRun){line, col, 1};
}

int ms_chunk_add_constant(MsChunk* c, MsValue val) {
    ms_value_array_push(&c->constants, val);
    return c->constants.count - 1;
}

int ms_chunk_get_line(const MsChunk* c, int offset) {
    int accum = 0;
    for (int i = 0; i < c->line_count; i++) {
        accum += c->lines[i].count;
        if (offset < accum) return c->lines[i].line;
    }
    /* Fallback: return last known line */
    if (c->line_count > 0) return c->lines[c->line_count - 1].line;
    return -1;
}

static const char* const k_opcode_names[MS_OP_COUNT] = {
    "LOADK", "LOADNIL", "LOADTRUE", "LOADFALSE", "MOVE",
    "GETGLOBAL", "SETGLOBAL", "DEFGLOBAL",
    "GETUPVAL", "SETUPVAL",
    "GETPROP", "SETPROP", "GETSUPER", "EXTRAARG",
    "ADD", "SUB", "MUL", "DIV", "MOD",
    "EQ", "LT", "LE",
    "NEG", "NOT", "STR",
    "BAND", "BOR", "BXOR", "BNOT", "SHL", "SHR",
    "JMP", "TEST", "TESTSET",
    "CALL", "INVOKE", "SUPERINV", "RETURN",
    "CLOSURE", "CLOSE",
    "CLASS", "INHERIT", "METHOD", "STATICMETH",
    "GETTER", "SETTER", "ABSTMETH",
    "NEWLIST", "NEWMAP", "NEWTUPLE", "GETIDX", "SETIDX",
    "IMPORT", "IMPFROM", "IMPALIAS",
    "FORITER",
    "THROW", "TRY", "ENDTRY",
    "DEFER",
    "YIELD", "RESUME",
    "ADD_II", "ADD_FF", "ADD_SS",
    "SUB_II", "SUB_FF",
    "MUL_II", "MUL_FF",
    "DIV_FF",
    "LT_II", "LT_FF",
    "EQ_II",
    "NOP",
};

const char* ms_opcode_name(MsOpCode op) {
    if (op < 0 || op >= MS_OP_COUNT) return "UNKNOWN";
    return k_opcode_names[op];
}
