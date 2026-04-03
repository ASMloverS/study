#ifndef MS_CHUNK_H
#define MS_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
	MS_OP_CONSTANT,
	MS_OP_NIL,
	MS_OP_TRUE,
	MS_OP_FALSE,
	MS_OP_POP,
	MS_OP_DEFINE_GLOBAL,
	MS_OP_GET_GLOBAL,
	MS_OP_SET_GLOBAL,
	MS_OP_GET_LOCAL,
	MS_OP_SET_LOCAL,
	MS_OP_GET_UPVALUE,
	MS_OP_SET_UPVALUE,
	MS_OP_GET_PROPERTY,
	MS_OP_SET_PROPERTY,
	MS_OP_GET_SUPER,
	MS_OP_BUILD_LIST,
	MS_OP_GET_SUBSCRIPT,
	MS_OP_SET_SUBSCRIPT,
	MS_OP_EQUAL,
	MS_OP_NOT_EQUAL,
	MS_OP_GREATER,
	MS_OP_GREATER_EQUAL,
	MS_OP_LESS,
	MS_OP_LESS_EQUAL,
	MS_OP_ADD,
	MS_OP_SUBTRACT,
	MS_OP_MULTIPLY,
	MS_OP_DIVIDE,
	MS_OP_MODULO,
	MS_OP_NEGATE,
	MS_OP_NOT,
	MS_OP_AND,
	MS_OP_OR,
	MS_OP_JUMP,
	MS_OP_JUMP_IF_FALSE,
	MS_OP_LOOP,
	MS_OP_BREAK,
	MS_OP_CONTINUE,
	MS_OP_CALL,
	MS_OP_INVOKE,
	MS_OP_SUPER_INVOKE,
	MS_OP_CLOSURE,
	MS_OP_CLOSE_UPVALUE,
	MS_OP_CLASS,
	MS_OP_INHERIT,
	MS_OP_METHOD,
	MS_OP_IMPORT,
	MS_OP_IMPORT_FROM,
	MS_OP_RETURN,
	MS_OP_DEBUG_BREAK
} MsOpCode;

typedef struct {
	uint8_t *code;
	int *lines;
	int count;
	int capacity;
	MsValueArray constants;
} MsChunk;

void ms_chunk_init(MsChunk *chunk);
void ms_chunk_free(MsChunk *chunk);
void ms_chunk_write(MsChunk *chunk, uint8_t byte, int line);
int ms_chunk_add_constant(MsChunk *chunk, MsValue value);
void ms_chunk_disassemble(const MsChunk *chunk, const char *name);
int ms_chunk_disassemble_instruction(const MsChunk *chunk, int offset);

#endif
