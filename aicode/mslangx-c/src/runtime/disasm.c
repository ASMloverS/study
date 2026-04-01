#include "ms/runtime/chunk.h"

#include <stdarg.h>
#include <stdio.h>

#include "ms/runtime/function.h"
#include "ms/runtime/opcode.h"

static const char* ms_disasm_opcode_name(uint8_t opcode) {
  switch ((MsOpcode) opcode) {
    case MS_OP_CONSTANT:
      return "OP_CONSTANT";
    case MS_OP_NIL:
      return "OP_NIL";
    case MS_OP_TRUE:
      return "OP_TRUE";
    case MS_OP_FALSE:
      return "OP_FALSE";
    case MS_OP_POP:
      return "OP_POP";
    case MS_OP_GET_LOCAL:
      return "OP_GET_LOCAL";
    case MS_OP_SET_LOCAL:
      return "OP_SET_LOCAL";
    case MS_OP_GET_UPVALUE:
      return "OP_GET_UPVALUE";
    case MS_OP_SET_UPVALUE:
      return "OP_SET_UPVALUE";
    case MS_OP_GET_GLOBAL:
      return "OP_GET_GLOBAL";
    case MS_OP_DEFINE_GLOBAL:
      return "OP_DEFINE_GLOBAL";
    case MS_OP_SET_GLOBAL:
      return "OP_SET_GLOBAL";
    case MS_OP_EQUAL:
      return "OP_EQUAL";
    case MS_OP_GREATER:
      return "OP_GREATER";
    case MS_OP_LESS:
      return "OP_LESS";
    case MS_OP_ADD:
      return "OP_ADD";
    case MS_OP_SUBTRACT:
      return "OP_SUBTRACT";
    case MS_OP_MULTIPLY:
      return "OP_MULTIPLY";
    case MS_OP_DIVIDE:
      return "OP_DIVIDE";
    case MS_OP_NOT:
      return "OP_NOT";
    case MS_OP_NEGATE:
      return "OP_NEGATE";
    case MS_OP_PRINT:
      return "OP_PRINT";
    case MS_OP_JUMP:
      return "OP_JUMP";
    case MS_OP_JUMP_IF_FALSE:
      return "OP_JUMP_IF_FALSE";
    case MS_OP_LOOP:
      return "OP_LOOP";
    case MS_OP_CALL:
      return "OP_CALL";
    case MS_OP_CLOSURE:
      return "OP_CLOSURE";
    case MS_OP_CLASS:
      return "OP_CLASS";
    case MS_OP_INHERIT:
      return "OP_INHERIT";
    case MS_OP_METHOD:
      return "OP_METHOD";
    case MS_OP_GET_PROPERTY:
      return "OP_GET_PROPERTY";
    case MS_OP_SET_PROPERTY:
      return "OP_SET_PROPERTY";
    case MS_OP_GET_SUPER:
      return "OP_GET_SUPER";
    case MS_OP_CLOSE_UPVALUE:
      return "OP_CLOSE_UPVALUE";
    case MS_OP_RETURN:
      return "OP_RETURN";
  }

  return "OP_UNKNOWN";
}

static int ms_disasm_appendf(MsBuffer* buffer, const char* format, ...) {
  char scratch[256];
  int written;
  va_list arguments;

  va_start(arguments, format);
  written = vsnprintf(scratch, sizeof(scratch), format, arguments);
  va_end(arguments);
  if (written < 0 || (size_t) written >= sizeof(scratch)) {
    return 0;
  }

  return ms_buffer_append(buffer, scratch, (size_t) written);
}

static int ms_disasm_append_prefix(const MsChunk* chunk,
                                   size_t offset,
                                   MsBuffer* buffer) {
  int line = 0;
  int previous_line = 0;

  if (!ms_chunk_get_line(chunk, offset, &line)) {
    return 0;
  }
  if (offset > 0 && ms_chunk_get_line(chunk, offset - 1, &previous_line) &&
      previous_line == line) {
    return ms_disasm_appendf(buffer, "%04zu    | ", offset);
  }

  return ms_disasm_appendf(buffer, "%04zu %4d ", offset, line);
}

static int ms_disasm_append_constant(MsBuffer* buffer,
                                     const char* name,
                                     uint8_t operand,
                                     MsValue constant) {
  char text[128];

  if (!ms_value_format(constant, text, sizeof(text))) {
    return 0;
  }

  return ms_disasm_appendf(buffer, "%-17s%u '%s'\n", name, operand, text);
}

static int ms_disasm_append_closure(const MsChunk* chunk,
                                    size_t offset,
                                    MsBuffer* buffer,
                                    size_t* out_next_offset) {
  uint8_t operand = 0;
  MsValue constant = ms_value_nil();
  MsFunction* function = NULL;
  size_t current_offset;
  size_t i;

  if (!ms_chunk_read_byte(chunk, offset + 1, &operand) ||
      !ms_chunk_get_constant(chunk, operand, &constant) ||
      !ms_value_get_function(constant, &function)) {
    return 0;
  }
  if (!ms_disasm_append_constant(buffer, "OP_CLOSURE", operand, constant)) {
    return 0;
  }

  current_offset = offset + 2;
  for (i = 0; i < function->upvalue_count; ++i) {
    uint8_t is_local = 0;
    uint8_t slot = 0;

    if (!ms_chunk_read_byte(chunk, current_offset, &is_local) ||
        !ms_chunk_read_byte(chunk, current_offset + 1, &slot) ||
        !ms_disasm_appendf(buffer,
                           "%04zu    |                     %s %u\n",
                           current_offset,
                           is_local != 0 ? "local" : "upvalue",
                           slot)) {
      return 0;
    }
    current_offset += 2;
  }

  *out_next_offset = current_offset;
  return 1;
}

static size_t ms_disassemble_instruction(const MsChunk* chunk,
                                         size_t offset,
                                         MsBuffer* buffer) {
  uint8_t opcode = 0;
  uint8_t operand = 0;
  uint16_t jump = 0;
  MsValue constant = ms_value_nil();
  const char* name;
  size_t target;
  size_t next_offset = 0;

  if (!ms_chunk_read_byte(chunk, offset, &opcode)) {
    return chunk->code.length;
  }
  if (!ms_disasm_append_prefix(chunk, offset, buffer)) {
    return chunk->code.length;
  }

  name = ms_disasm_opcode_name(opcode);
  switch ((MsOpcode) opcode) {
    case MS_OP_CONSTANT:
    case MS_OP_GET_GLOBAL:
    case MS_OP_DEFINE_GLOBAL:
    case MS_OP_SET_GLOBAL:
    case MS_OP_CLASS:
    case MS_OP_METHOD:
    case MS_OP_GET_PROPERTY:
    case MS_OP_SET_PROPERTY:
    case MS_OP_GET_SUPER:
      if (!ms_chunk_read_byte(chunk, offset + 1, &operand) ||
          !ms_chunk_get_constant(chunk, operand, &constant) ||
          !ms_disasm_append_constant(buffer, name, operand, constant)) {
        return chunk->code.length;
      }
      return offset + 2;
    case MS_OP_GET_LOCAL:
    case MS_OP_SET_LOCAL:
    case MS_OP_GET_UPVALUE:
    case MS_OP_SET_UPVALUE:
    case MS_OP_CALL:
      if (!ms_chunk_read_byte(chunk, offset + 1, &operand) ||
          !ms_disasm_appendf(buffer, "%-17s%u\n", name, operand)) {
        return chunk->code.length;
      }
      return offset + 2;
    case MS_OP_CLOSURE:
      if (!ms_disasm_append_closure(chunk, offset, buffer, &next_offset)) {
        return chunk->code.length;
      }
      return next_offset;
    case MS_OP_JUMP:
    case MS_OP_JUMP_IF_FALSE:
      if (!ms_chunk_read_short(chunk, offset + 1, &jump)) {
        return chunk->code.length;
      }
      target = offset + 3 + (size_t) jump;
      if (!ms_disasm_appendf(buffer, "%-17s%zu -> %zu\n", name, offset, target)) {
        return chunk->code.length;
      }
      return offset + 3;
    case MS_OP_LOOP:
      if (!ms_chunk_read_short(chunk, offset + 1, &jump) ||
          offset + 3 < (size_t) jump) {
        return chunk->code.length;
      }
      target = offset + 3 - (size_t) jump;
      if (!ms_disasm_appendf(buffer, "%-17s%zu -> %zu\n", name, offset, target)) {
        return chunk->code.length;
      }
      return offset + 3;
    case MS_OP_NIL:
    case MS_OP_TRUE:
    case MS_OP_FALSE:
    case MS_OP_POP:
    case MS_OP_EQUAL:
    case MS_OP_GREATER:
    case MS_OP_LESS:
    case MS_OP_ADD:
    case MS_OP_SUBTRACT:
    case MS_OP_MULTIPLY:
    case MS_OP_DIVIDE:
    case MS_OP_NOT:
    case MS_OP_NEGATE:
    case MS_OP_PRINT:
    case MS_OP_INHERIT:
    case MS_OP_CLOSE_UPVALUE:
    case MS_OP_RETURN:
      if (!ms_disasm_appendf(buffer, "%s\n", name)) {
        return chunk->code.length;
      }
      return offset + 1;
  }

  if (!ms_disasm_appendf(buffer, "OP_UNKNOWN_%u\n", opcode)) {
    return chunk->code.length;
  }
  return offset + 1;
}

int ms_chunk_disassemble_to_buffer(const MsChunk* chunk,
                                   const char* name,
                                   MsBuffer* buffer) {
  size_t offset = 0;
  const char* label = name == NULL ? "<chunk>" : name;

  if (chunk == NULL || buffer == NULL) {
    return 0;
  }

  ms_buffer_clear(buffer);
  if (!ms_disasm_appendf(buffer, "== %s ==\n", label)) {
    return 0;
  }

  while (offset < chunk->code.length) {
    size_t next_offset = ms_disassemble_instruction(chunk, offset, buffer);

    if (next_offset <= offset) {
      return 0;
    }
    offset = next_offset;
  }

  return 1;
}