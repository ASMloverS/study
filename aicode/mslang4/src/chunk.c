#include "chunk.h"
#include "memory.h"
#include <stdio.h>

void ms_chunk_init(MsChunk *chunk)
{
	chunk->code = NULL;
	chunk->lines = NULL;
	chunk->count = 0;
	chunk->capacity = 0;
	ms_value_array_init(&chunk->constants);
}

void ms_chunk_free(MsChunk *chunk)
{
	MS_FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
	MS_FREE_ARRAY(int, chunk->lines, chunk->capacity);
	ms_value_array_free(&chunk->constants);
	ms_chunk_init(chunk);
}

void ms_chunk_write(MsChunk *chunk, uint8_t byte, int line)
{
	if (chunk->capacity < chunk->count + 1) {
		int old_capacity = chunk->capacity;
		chunk->capacity = MS_GROW_CAPACITY(old_capacity);
		chunk->code = MS_GROW_ARRAY(uint8_t, chunk->code,
					    old_capacity, chunk->capacity);
		chunk->lines = MS_GROW_ARRAY(int, chunk->lines,
					     old_capacity, chunk->capacity);
	}
	chunk->code[chunk->count] = byte;
	chunk->lines[chunk->count] = line;
	chunk->count++;
}

int ms_chunk_add_constant(MsChunk *chunk, MsValue value)
{
	ms_value_array_write(&chunk->constants, value);
	return chunk->constants.count - 1;
}

static const char *ms_opcode_name(MsOpCode code)
{
	switch (code) {
	case MS_OP_CONSTANT:       return "OP_CONSTANT";
	case MS_OP_NIL:            return "OP_NIL";
	case MS_OP_TRUE:           return "OP_TRUE";
	case MS_OP_FALSE:          return "OP_FALSE";
	case MS_OP_POP:            return "OP_POP";
	case MS_OP_DEFINE_GLOBAL:  return "OP_DEFINE_GLOBAL";
	case MS_OP_GET_GLOBAL:     return "OP_GET_GLOBAL";
	case MS_OP_SET_GLOBAL:     return "OP_SET_GLOBAL";
	case MS_OP_GET_LOCAL:      return "OP_GET_LOCAL";
	case MS_OP_SET_LOCAL:      return "OP_SET_LOCAL";
	case MS_OP_GET_UPVALUE:    return "OP_GET_UPVALUE";
	case MS_OP_SET_UPVALUE:    return "OP_SET_UPVALUE";
	case MS_OP_GET_PROPERTY:   return "OP_GET_PROPERTY";
	case MS_OP_SET_PROPERTY:   return "OP_SET_PROPERTY";
	case MS_OP_GET_SUPER:      return "OP_GET_SUPER";
	case MS_OP_BUILD_LIST:     return "OP_BUILD_LIST";
	case MS_OP_GET_SUBSCRIPT:  return "OP_GET_SUBSCRIPT";
	case MS_OP_SET_SUBSCRIPT:  return "OP_SET_SUBSCRIPT";
	case MS_OP_EQUAL:          return "OP_EQUAL";
	case MS_OP_NOT_EQUAL:      return "OP_NOT_EQUAL";
	case MS_OP_GREATER:        return "OP_GREATER";
	case MS_OP_GREATER_EQUAL:  return "OP_GREATER_EQUAL";
	case MS_OP_LESS:           return "OP_LESS";
	case MS_OP_LESS_EQUAL:     return "OP_LESS_EQUAL";
	case MS_OP_ADD:            return "OP_ADD";
	case MS_OP_SUBTRACT:       return "OP_SUBTRACT";
	case MS_OP_MULTIPLY:       return "OP_MULTIPLY";
	case MS_OP_DIVIDE:         return "OP_DIVIDE";
	case MS_OP_MODULO:         return "OP_MODULO";
	case MS_OP_NEGATE:         return "OP_NEGATE";
	case MS_OP_NOT:            return "OP_NOT";
	case MS_OP_AND:            return "OP_AND";
	case MS_OP_OR:             return "OP_OR";
	case MS_OP_JUMP:           return "OP_JUMP";
	case MS_OP_JUMP_IF_FALSE:  return "OP_JUMP_IF_FALSE";
	case MS_OP_LOOP:           return "OP_LOOP";
	case MS_OP_BREAK:          return "OP_BREAK";
	case MS_OP_CONTINUE:       return "OP_CONTINUE";
	case MS_OP_CALL:           return "OP_CALL";
	case MS_OP_INVOKE:         return "OP_INVOKE";
	case MS_OP_SUPER_INVOKE:   return "OP_SUPER_INVOKE";
	case MS_OP_CLOSURE:        return "OP_CLOSURE";
	case MS_OP_CLOSE_UPVALUE:  return "OP_CLOSE_UPVALUE";
	case MS_OP_CLASS:          return "OP_CLASS";
	case MS_OP_INHERIT:        return "OP_INHERIT";
	case MS_OP_METHOD:         return "OP_METHOD";
	case MS_OP_IMPORT:         return "OP_IMPORT";
	case MS_OP_IMPORT_FROM:    return "OP_IMPORT_FROM";
	case MS_OP_RETURN:         return "OP_RETURN";
	case MS_OP_DEBUG_BREAK:    return "OP_DEBUG_BREAK";
	default:                   return "OP_UNKNOWN";
	}
}

static int ms_disassemble_simple(const char *name,
				 __attribute__((unused)) const MsChunk *chunk,
				 int offset)
{
	printf("%04d %s\n", offset, name);
	return offset + 1;
}

void ms_chunk_disassemble(const MsChunk *chunk, const char *name)
{
	printf("== %s ==\n", name);
	for (int offset = 0; offset < chunk->count;) {
		offset = ms_chunk_disassemble_instruction(chunk, offset);
	}
}

static int ms_disassemble_constant(const char *name, const MsChunk *chunk,
				  int offset)
{
	uint8_t constant_idx = chunk->code[offset + 1];
	printf("%04d %-16s %4d '", offset, name, constant_idx);
	ms_print_value(chunk->constants.values[constant_idx]);
	printf("'\n");
	return offset + 2;
}

static int ms_disassemble_jump(const char *name, int sign,
			       const MsChunk *chunk, int offset)
{
	uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) |
				   chunk->code[offset + 2]);
	printf("%04d %-16s %4d -> %d\n", offset, name,
	       offset + 3 + sign * jump, offset + 3);
	return offset + 3;
}

int ms_chunk_disassemble_instruction(const MsChunk *chunk, int offset)
{
	uint8_t instruction = chunk->code[offset];
	switch (instruction) {
	case MS_OP_CONSTANT:
		return ms_disassemble_constant(
			ms_opcode_name(instruction), chunk, offset);
	case MS_OP_JUMP:
	case MS_OP_JUMP_IF_FALSE:
	case MS_OP_LOOP:
		return ms_disassemble_jump(
			ms_opcode_name(instruction), 1, chunk, offset);
	case MS_OP_NIL:
	case MS_OP_TRUE:
	case MS_OP_FALSE:
	case MS_OP_POP:
	case MS_OP_RETURN:
	case MS_OP_NEGATE:
	case MS_OP_NOT:
	case MS_OP_CLOSE_UPVALUE:
	case MS_OP_INHERIT:
	case MS_OP_DEBUG_BREAK:
		return ms_disassemble_simple(
			ms_opcode_name(instruction), chunk, offset);
	case MS_OP_DEFINE_GLOBAL:
	case MS_OP_GET_GLOBAL:
	case MS_OP_SET_GLOBAL:
	case MS_OP_CALL:
	case MS_OP_CLASS:
	case MS_OP_METHOD:
		printf("%04d %-16s %4d\n", offset,
		       ms_opcode_name(instruction), chunk->code[offset + 1]);
		return offset + 2;
	default:
		printf("%04d Unknown opcode %d\n", offset, instruction);
		return offset + 1;
	}
}
