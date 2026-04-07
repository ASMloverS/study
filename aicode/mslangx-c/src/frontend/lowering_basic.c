#include "ms/frontend/lowering.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ms/arena.h"
#include "ms/diag.h"
#include "ms/frontend/resolver.h"
#include "ms/parser.h"
#include "ms/runtime/function.h"
#include "ms/runtime/opcode.h"
#include "ms/string.h"
#include "ms/value.h"

typedef struct MsActiveLocal {
  uint8_t slot;
  int scope_depth;
  int is_captured;
} MsActiveLocal;

typedef struct MsLoopContext {
  size_t continue_target;
  int continue_scope_depth;
  int break_scope_depth;
  size_t* break_patches;
  size_t break_count;
  size_t break_capacity;
  struct MsLoopContext* enclosing;
} MsLoopContext;

typedef struct MsLoweringFunctionContext {
  size_t node_id;
  MsChunk* chunk;
  MsFunctionResolution resolution;
  MsActiveLocal* locals;
  size_t local_count;
  size_t local_capacity;
  int scope_depth;
  MsLoopContext* current_loop;
  struct MsLoweringFunctionContext* enclosing;
} MsLoweringFunctionContext;

typedef struct MsLoweringState {
  const char* file;
  const MsResolutionTable* resolution;
  MsDiagnosticList* diagnostics;
  MsLoweringFunctionContext* current_function;
} MsLoweringState;

static int ms_lowering_append_diagnostic(MsLoweringState* state,
                                         const MsAstNode* node,
                                         const char* code,
                                         const char* message) {
  MsDiagnostic diagnostic;

  if (state == NULL || node == NULL || state->diagnostics == NULL) {
    return 0;
  }

  diagnostic.phase = "resolve";
  diagnostic.code = code;
  diagnostic.message = message;
  diagnostic.span.file = state->file;
  diagnostic.span.line = node->line;
  diagnostic.span.column = node->column;
  diagnostic.span.length = node->end_column - node->column + 1;
  return ms_diag_list_append(state->diagnostics, &diagnostic);
}

static int ms_lowering_ensure_locals(MsLoweringFunctionContext* function,
                                     size_t min_capacity) {
  MsActiveLocal* locals;
  size_t new_capacity;

  if (function == NULL) {
    return 0;
  }
  if (min_capacity <= function->local_capacity) {
    return 1;
  }

  new_capacity = function->local_capacity == 0 ? 16 : function->local_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  locals = (MsActiveLocal*) realloc(function->locals,
                                    new_capacity * sizeof(*locals));
  if (locals == NULL) {
    return 0;
  }

  function->locals = locals;
  function->local_capacity = new_capacity;
  return 1;
}

static int ms_loop_ensure_breaks(MsLoopContext* loop, size_t min_capacity) {
  size_t* patches;
  size_t new_capacity;

  if (loop == NULL) {
    return 0;
  }
  if (min_capacity <= loop->break_capacity) {
    return 1;
  }

  new_capacity = loop->break_capacity == 0 ? 8 : loop->break_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  patches = (size_t*) realloc(loop->break_patches,
                              new_capacity * sizeof(*patches));
  if (patches == NULL) {
    return 0;
  }

  loop->break_patches = patches;
  loop->break_capacity = new_capacity;
  return 1;
}

static MsChunk* ms_lowering_current_chunk(MsLoweringState* state) {
  return state != NULL && state->current_function != NULL ?
      state->current_function->chunk : NULL;
}

static int ms_lowering_emit_byte(MsLoweringState* state,
                                 uint8_t byte,
                                 int line) {
  MsChunk* chunk = ms_lowering_current_chunk(state);

  return chunk != NULL && ms_chunk_write(chunk, byte, line);
}

static int ms_lowering_emit_opcode(MsLoweringState* state,
                                   MsOpcode opcode,
                                   int line) {
  return ms_lowering_emit_byte(state, (uint8_t) opcode, line);
}

static int ms_lowering_emit_short(MsLoweringState* state,
                                  uint16_t value,
                                  int line) {
  MsChunk* chunk = ms_lowering_current_chunk(state);

  return chunk != NULL && ms_chunk_write_short(chunk, value, line);
}

static int ms_lowering_emit_jump(MsLoweringState* state,
                                 MsOpcode opcode,
                                 int line,
                                 size_t* out_patch_offset) {
  MsChunk* chunk = ms_lowering_current_chunk(state);

  if (chunk == NULL || out_patch_offset == NULL ||
      !ms_lowering_emit_opcode(state, opcode, line)) {
    return 0;
  }

  *out_patch_offset = ms_chunk_code_count(chunk);
  return ms_lowering_emit_short(state, 0xffffu, line);
}

static int ms_lowering_patch_jump(MsLoweringState* state, size_t patch_offset) {
  MsChunk* chunk = ms_lowering_current_chunk(state);
  size_t code_count;
  size_t distance;

  if (chunk == NULL) {
    return 0;
  }

  code_count = ms_chunk_code_count(chunk);
  if (code_count < patch_offset + 2) {
    return 0;
  }

  distance = code_count - patch_offset - 2;
  if (distance > UINT16_MAX) {
    return 0;
  }

  return ms_chunk_patch_short(chunk, patch_offset, (uint16_t) distance);
}

static int ms_lowering_emit_loop(MsLoweringState* state,
                                 size_t target,
                                 int line) {
  MsChunk* chunk = ms_lowering_current_chunk(state);
  size_t distance;

  if (chunk == NULL || !ms_lowering_emit_opcode(state, MS_OP_LOOP, line)) {
    return 0;
  }

  distance = ms_chunk_code_count(chunk) + 2 - target;
  if (distance > UINT16_MAX) {
    return 0;
  }

  return ms_lowering_emit_short(state, (uint16_t) distance, line);
}

static int ms_lowering_copy_token_text(MsToken token,
                                       size_t trim_left,
                                       size_t trim_right,
                                       char** out_text,
                                       size_t* out_length) {
  char* text;
  size_t length;

  if (out_text == NULL || out_length == NULL ||
      token.length < trim_left + trim_right) {
    return 0;
  }

  length = token.length - trim_left - trim_right;
  text = (char*) malloc(length + 1);
  if (text == NULL) {
    return 0;
  }

  memcpy(text, token.start + trim_left, length);
  text[length] = '\0';
  *out_text = text;
  *out_length = length;
  return 1;
}

static int ms_lowering_emit_constant_value(MsLoweringState* state,
                                           const MsAstNode* node,
                                           MsValue value) {
  MsChunk* chunk = ms_lowering_current_chunk(state);
  uint8_t constant_index = 0;

  if (chunk == NULL || state == NULL || node == NULL ||
      !ms_chunk_add_constant(chunk, value, &constant_index) ||
      !ms_lowering_emit_opcode(state, MS_OP_CONSTANT, node->line)) {
    return 0;
  }

  return ms_lowering_emit_byte(state, constant_index, node->line);
}

static int ms_lowering_emit_name_operand(MsLoweringState* state,
                                         MsToken token,
                                         int line,
                                         uint8_t* out_index) {
  MsChunk* chunk = ms_lowering_current_chunk(state);
  char* text = NULL;
  size_t length = 0;
  MsString* string = NULL;

  (void) line;
  if (chunk == NULL || out_index == NULL ||
      !ms_lowering_copy_token_text(token, 0, 0, &text, &length)) {
    return 0;
  }

  string = ms_string_new(text, length);
  free(text);
  if (string == NULL) {
    return 0;
  }

  if (!ms_chunk_add_constant(chunk,
                             ms_value_object((MsObject*) string),
                             out_index)) {
    return 0;
  }
  return 1;
}

static int ms_lowering_emit_text_operand(MsLoweringState* state,
                                         const char* text,
                                         size_t length,
                                         uint8_t* out_index) {
  MsChunk* chunk = ms_lowering_current_chunk(state);
  MsString* string = NULL;

  if (chunk == NULL || text == NULL || out_index == NULL) {
    return 0;
  }

  string = ms_string_new(text, length);
  if (string == NULL) {
    return 0;
  }

  if (!ms_chunk_add_constant(chunk,
                             ms_value_object((MsObject*) string),
                             out_index)) {
    return 0;
  }
  return 1;
}

static int ms_lowering_token_present(MsToken token) {
  return token.start != NULL && token.length > 0;
}

static MsToken ms_lowering_import_binding_name(const MsAstNode* node) {
  MsToken empty_token;

  memset(&empty_token, 0, sizeof(empty_token));
  if (node == NULL) {
    return empty_token;
  }

  switch (node->kind) {
    case MS_AST_IMPORT_STMT:
      if (ms_lowering_token_present(node->as.import_stmt.alias)) {
        return node->as.import_stmt.alias;
      }
      if (node->as.import_stmt.path.count > 0) {
        return node->as.import_stmt.path.items[node->as.import_stmt.path.count - 1];
      }
      break;
    case MS_AST_FROM_IMPORT_STMT:
      if (ms_lowering_token_present(node->as.from_import_stmt.alias)) {
        return node->as.from_import_stmt.alias;
      }
      return node->as.from_import_stmt.name;
    default:
      break;
  }

  return empty_token;
}

static int ms_lowering_copy_module_path_text(const MsTokenArray* path,
                                             char** out_text,
                                             size_t* out_length) {
  char* text;
  size_t i;
  size_t length = 0;
  size_t offset = 0;

  if (path == NULL || out_text == NULL || out_length == NULL ||
      path->count == 0) {
    return 0;
  }

  for (i = 0; i < path->count; ++i) {
    length += path->items[i].length;
    if (i + 1 < path->count) {
      length += 1;
    }
  }

  text = (char*) malloc(length + 1);
  if (text == NULL) {
    return 0;
  }

  for (i = 0; i < path->count; ++i) {
    memcpy(text + offset, path->items[i].start, path->items[i].length);
    offset += path->items[i].length;
    if (i + 1 < path->count) {
      text[offset] = '.';
      offset += 1;
    }
  }

  text[length] = '\0';
  *out_text = text;
  *out_length = length;
  return 1;
}

static int ms_lowering_emit_module_path_operand(MsLoweringState* state,
                                                const MsTokenArray* path,
                                                uint8_t* out_index) {
  char* text = NULL;
  size_t length = 0;
  int result;

  if (!ms_lowering_copy_module_path_text(path, &text, &length)) {
    return 0;
  }

  result = ms_lowering_emit_text_operand(state, text, length, out_index);
  free(text);
  return result;
}

static int ms_lowering_emit_named_opcode(MsLoweringState* state,
                                         MsOpcode opcode,
                                         MsToken token,
                                         int line) {
  uint8_t name_index = 0;

  return ms_lowering_emit_name_operand(state, token, line, &name_index) &&
         ms_lowering_emit_opcode(state, opcode, line) &&
         ms_lowering_emit_byte(state, name_index, line);
}

static int ms_lowering_get_binding(MsLoweringState* state,
                                   const MsAstNode* node,
                                   MsResolvedBinding* out_binding) {
  if (state == NULL || node == NULL || out_binding == NULL) {
    return 0;
  }

  if (!ms_resolution_table_get(state->resolution, node->node_id, out_binding)) {
    return ms_lowering_append_diagnostic(state,
                                         node,
                                         "MS3006",
                                         "missing resolution entry for node");
  }

  return 1;
}

static int ms_lowering_get_function_resolution(MsLoweringState* state,
                                               size_t node_id,
                                               MsFunctionResolution* out_function) {
  if (state == NULL || out_function == NULL) {
    return 0;
  }

  return ms_resolution_table_get_function(state->resolution, node_id, out_function);
}

static int ms_lowering_push_local(MsLoweringState* state,
                                  uint8_t slot,
                                  int scope_depth,
                                  int is_captured);

static int ms_lowering_emit_import_binding(MsLoweringState* state,
                                           const MsAstNode* node) {
  MsResolvedBinding binding;
  MsToken binding_name;
  uint8_t name_index = 0;

  if (!ms_lowering_get_binding(state, node, &binding)) {
    return 0;
  }

  binding_name = ms_lowering_import_binding_name(node);
  if (!ms_lowering_token_present(binding_name)) {
    return 0;
  }

  if (binding.kind == MS_BINDING_LOCAL) {
    return ms_lowering_push_local(state,
                                  binding.slot,
                                  binding.scope_depth,
                                  binding.is_captured);
  }
  if (!ms_lowering_emit_name_operand(state,
                                     binding_name,
                                     node->line,
                                     &name_index) ||
      !ms_lowering_emit_opcode(state, MS_OP_DEFINE_GLOBAL, node->line)) {
    return 0;
  }
  return ms_lowering_emit_byte(state, name_index, node->line);
}

static int ms_lowering_emit_import_module_stmt(MsLoweringState* state,
                                               const MsAstNode* node) {
  uint8_t module_index = 0;

  if (state == NULL || node == NULL || node->kind != MS_AST_IMPORT_STMT ||
      !ms_lowering_emit_module_path_operand(state,
                                            &node->as.import_stmt.path,
                                            &module_index) ||
      !ms_lowering_emit_opcode(state, MS_OP_IMPORT_MODULE, node->line) ||
      !ms_lowering_emit_byte(state, module_index, node->line)) {
    return 0;
  }

  return ms_lowering_emit_import_binding(state, node);
}

static int ms_lowering_emit_from_import_stmt(MsLoweringState* state,
                                             const MsAstNode* node) {
  uint8_t module_index = 0;
  uint8_t symbol_index = 0;

  if (state == NULL || node == NULL || node->kind != MS_AST_FROM_IMPORT_STMT ||
      !ms_lowering_emit_module_path_operand(state,
                                            &node->as.from_import_stmt.path,
                                            &module_index) ||
      !ms_lowering_emit_name_operand(state,
                                     node->as.from_import_stmt.name,
                                     node->line,
                                     &symbol_index) ||
      !ms_lowering_emit_opcode(state, MS_OP_IMPORT_SYMBOL, node->line) ||
      !ms_lowering_emit_byte(state, module_index, node->line) ||
      !ms_lowering_emit_byte(state, symbol_index, node->line)) {
    return 0;
  }

  return ms_lowering_emit_import_binding(state, node);
}

static int ms_lowering_function_local_is_captured(
    const MsLoweringFunctionContext* function,
    uint8_t slot) {
  size_t i;

  if (function == NULL) {
    return 0;
  }

  for (i = 0; i < function->resolution.captured_local_count; ++i) {
    if (function->resolution.captured_locals[i] == slot) {
      return 1;
    }
  }

  return 0;
}

static int ms_lowering_push_local(MsLoweringState* state,
                                  uint8_t slot,
                                  int scope_depth,
                                  int is_captured) {
  MsLoweringFunctionContext* function = state != NULL ?
      state->current_function : NULL;

  if (function == NULL ||
      !ms_lowering_ensure_locals(function, function->local_count + 1)) {
    return 0;
  }

  function->locals[function->local_count].slot = slot;
  function->locals[function->local_count].scope_depth = scope_depth;
  function->locals[function->local_count].is_captured = is_captured;
  function->local_count += 1;
  return 1;
}

static int ms_lowering_begin_scope(MsLoweringState* state) {
  if (state == NULL || state->current_function == NULL) {
    return 0;
  }

  state->current_function->scope_depth += 1;
  return 1;
}

static int ms_lowering_emit_pop_local(MsLoweringState* state,
                                      MsActiveLocal local,
                                      int line) {
  return ms_lowering_emit_opcode(state,
                                 local.is_captured ? MS_OP_CLOSE_UPVALUE : MS_OP_POP,
                                 line);
}

static int ms_lowering_pop_locals_to_depth(MsLoweringState* state,
                                           int target_depth,
                                           int line) {
  MsLoweringFunctionContext* function = state != NULL ?
      state->current_function : NULL;

  if (function == NULL) {
    return 0;
  }

  while (function->local_count > 0 &&
         function->locals[function->local_count - 1].scope_depth > target_depth) {
    if (!ms_lowering_emit_pop_local(state,
                                    function->locals[function->local_count - 1],
                                    line)) {
      return 0;
    }
    function->local_count -= 1;
  }

  return 1;
}

static int ms_lowering_end_scope(MsLoweringState* state, int line) {
  MsLoweringFunctionContext* function = state != NULL ?
      state->current_function : NULL;

  if (function == NULL) {
    return 0;
  }

  while (function->local_count > 0 &&
         function->locals[function->local_count - 1].scope_depth ==
             function->scope_depth) {
    if (!ms_lowering_emit_pop_local(state,
                                    function->locals[function->local_count - 1],
                                    line)) {
      return 0;
    }
    function->local_count -= 1;
  }

  function->scope_depth -= 1;
  return 1;
}

static int ms_lowering_emit_break(MsLoweringState* state, const MsAstNode* node) {
  size_t patch_offset = 0;
  MsLoopContext* loop = state != NULL && state->current_function != NULL ?
      state->current_function->current_loop : NULL;

  if (loop == NULL) {
    return ms_lowering_append_diagnostic(state,
                                         node,
                                         "MS3003",
                                         "break is only allowed inside loops");
  }
  if (!ms_lowering_pop_locals_to_depth(state,
                                       loop->break_scope_depth,
                                       node->line) ||
      !ms_lowering_emit_jump(state,
                             MS_OP_JUMP,
                             node->line,
                             &patch_offset) ||
      !ms_loop_ensure_breaks(loop, loop->break_count + 1)) {
    return 0;
  }

  loop->break_patches[loop->break_count] = patch_offset;
  loop->break_count += 1;
  return 1;
}

static int ms_lowering_emit_continue(MsLoweringState* state,
                                     const MsAstNode* node) {
  MsLoopContext* loop = state != NULL && state->current_function != NULL ?
      state->current_function->current_loop : NULL;

  if (loop == NULL) {
    return ms_lowering_append_diagnostic(state,
                                         node,
                                         "MS3003",
                                         "continue is only allowed inside loops");
  }

  return ms_lowering_pop_locals_to_depth(state,
                                         loop->continue_scope_depth,
                                         node->line) &&
         ms_lowering_emit_loop(state,
                               loop->continue_target,
                               node->line);
}

static int ms_lowering_patch_breaks(MsLoweringState* state, MsLoopContext* loop) {
  size_t i;

  if (state == NULL || loop == NULL) {
    return 0;
  }

  for (i = 0; i < loop->break_count; ++i) {
    if (!ms_lowering_patch_jump(state, loop->break_patches[i])) {
      return 0;
    }
  }

  return 1;
}

static int ms_lowering_emit_expression(MsLoweringState* state,
                                       const MsAstNode* node);
static int ms_lowering_emit_statement(MsLoweringState* state,
                                      const MsAstNode* node);
static int ms_lowering_emit_closure(MsLoweringState* state,
                                    const MsAstNode* node,
                                    MsFunction* function,
                                    size_t function_node_id);
static int ms_lowering_compile_function(MsLoweringState* state,
                                        size_t node_id,
                                        MsToken name,
                                        const MsTokenArray* parameters,
                                        const MsAstNode* body,
                                        MsFunction** out_function);
static int ms_lowering_emit_bound_get(MsLoweringState* state,
                                      const MsAstNode* node,
                                      MsToken name_token) {
  MsResolvedBinding binding;
  uint8_t name_index = 0;

  if (!ms_lowering_get_binding(state, node, &binding)) {
    return 0;
  }

  switch (binding.kind) {
    case MS_BINDING_LOCAL:
      return ms_lowering_emit_opcode(state, MS_OP_GET_LOCAL, node->line) &&
             ms_lowering_emit_byte(state, binding.slot, node->line);
    case MS_BINDING_UPVALUE:
      return ms_lowering_emit_opcode(state, MS_OP_GET_UPVALUE, node->line) &&
             ms_lowering_emit_byte(state, binding.slot, node->line);
    case MS_BINDING_GLOBAL:
      if (!ms_lowering_emit_name_operand(state, name_token, node->line, &name_index)) {
        return 0;
      }
      return ms_lowering_emit_opcode(state, MS_OP_GET_GLOBAL, node->line) &&
             ms_lowering_emit_byte(state, name_index, node->line);
  }

  return 0;
}

static int ms_lowering_emit_bound_set(MsLoweringState* state,
                                      const MsAstNode* node,
                                      MsToken name_token) {
  MsResolvedBinding binding;
  uint8_t name_index = 0;

  if (!ms_lowering_get_binding(state, node, &binding)) {
    return 0;
  }

  switch (binding.kind) {
    case MS_BINDING_LOCAL:
      return ms_lowering_emit_opcode(state, MS_OP_SET_LOCAL, node->line) &&
             ms_lowering_emit_byte(state, binding.slot, node->line);
    case MS_BINDING_UPVALUE:
      return ms_lowering_emit_opcode(state, MS_OP_SET_UPVALUE, node->line) &&
             ms_lowering_emit_byte(state, binding.slot, node->line);
    case MS_BINDING_GLOBAL:
      if (!ms_lowering_emit_name_operand(state, name_token, node->line, &name_index)) {
        return 0;
      }
      return ms_lowering_emit_opcode(state, MS_OP_SET_GLOBAL, node->line) &&
             ms_lowering_emit_byte(state, name_index, node->line);
  }

  return 0;
}

static int ms_lowering_emit_class_decl(MsLoweringState* state,
                                       const MsAstNode* node) {
  MsResolvedBinding binding;
  int has_superclass;
  size_t i;

  if (state == NULL || node == NULL) {
    return 0;
  }
  has_superclass = node->as.class_decl.superclass.start != NULL &&
      node->as.class_decl.superclass.length > 0;
  if (!ms_lowering_get_binding(state, node, &binding)) {
    return 0;
  }
  if (binding.kind == MS_BINDING_LOCAL && has_superclass) {
    return ms_lowering_append_diagnostic(state,
                                         node,
                                         "MS3005",
                                         "unsupported feature in basic lowering");
  }

  if (binding.kind == MS_BINDING_LOCAL) {
    if (!ms_lowering_emit_opcode(state, MS_OP_NIL, node->line) ||
        !ms_lowering_push_local(state,
                                binding.slot,
                                binding.scope_depth,
                                binding.is_captured) ||
        !ms_lowering_emit_named_opcode(state,
                                       MS_OP_CLASS,
                                       node->as.class_decl.name,
                                       node->line) ||
        !ms_lowering_emit_opcode(state, MS_OP_SET_LOCAL, node->line) ||
        !ms_lowering_emit_byte(state, binding.slot, node->line)) {
      return 0;
    }
  } else {
    if (!ms_lowering_emit_named_opcode(state,
                                       MS_OP_CLASS,
                                       node->as.class_decl.name,
                                       node->line) ||
        !ms_lowering_emit_named_opcode(state,
                                       MS_OP_DEFINE_GLOBAL,
                                       node->as.class_decl.name,
                                       node->line) ||
        !ms_lowering_emit_named_opcode(state,
                                       MS_OP_GET_GLOBAL,
                                       node->as.class_decl.name,
                                       node->line)) {
      return 0;
    }
    if (has_superclass &&
        (!ms_lowering_emit_named_opcode(state,
                                        MS_OP_GET_GLOBAL,
                                        node->as.class_decl.superclass,
                                        node->line) ||
         !ms_lowering_emit_opcode(state, MS_OP_INHERIT, node->line))) {
      return 0;
    }
  }

  for (i = 0; i < node->as.class_decl.methods.count; ++i) {
    const MsAstNode* method = node->as.class_decl.methods.items[i];
    MsFunction* function = NULL;

    if (method == NULL || method->kind != MS_AST_FUNCTION_DECL) {
      return ms_lowering_append_diagnostic(state,
                                           node,
                                           "MS3005",
                                           "unsupported feature in basic lowering");
    }
    if (!ms_lowering_compile_function(state,
                                      method->node_id,
                                      method->as.function_decl.name,
                                      &method->as.function_decl.parameters,
                                      method->as.function_decl.body,
                                      &function) ||
        !ms_lowering_emit_closure(state,
                                  method,
                                  function,
                                  method->node_id) ||
        !ms_lowering_emit_named_opcode(state,
                                       MS_OP_METHOD,
                                       method->as.function_decl.name,
                                       method->line)) {
      return 0;
    }
  }

  return ms_lowering_emit_opcode(state, MS_OP_POP, node->line);
}

static int ms_lowering_emit_literal(MsLoweringState* state,
                                    const MsAstNode* node) {
  char* text = NULL;
  size_t length = 0;
  char* end = NULL;
  double number = 0.0;
  MsString* string = NULL;

  switch (node->as.literal.token.kind) {
    case MS_TOKEN_TRUE:
      return ms_lowering_emit_opcode(state, MS_OP_TRUE, node->line);
    case MS_TOKEN_FALSE:
      return ms_lowering_emit_opcode(state, MS_OP_FALSE, node->line);
    case MS_TOKEN_NIL:
      return ms_lowering_emit_opcode(state, MS_OP_NIL, node->line);
    case MS_TOKEN_NUMBER:
      if (!ms_lowering_copy_token_text(node->as.literal.token,
                                       0,
                                       0,
                                       &text,
                                       &length)) {
        return 0;
      }
      errno = 0;
      number = strtod(text, &end);
      if (errno != 0 || end == NULL || *end != '\0') {
        free(text);
        return 0;
      }
      free(text);
      return ms_lowering_emit_constant_value(state,
                                             node,
                                             ms_value_number(number));
    case MS_TOKEN_STRING:
      if (!ms_lowering_copy_token_text(node->as.literal.token,
                                       1,
                                       1,
                                       &text,
                                       &length)) {
        return 0;
      }
      string = ms_string_new(text, length);
      free(text);
      if (string == NULL) {
        return 0;
      }
      return ms_lowering_emit_constant_value(state,
                                             node,
                                             ms_value_object((MsObject*) string));
    default:
      return 0;
  }
}

static int ms_lowering_emit_list_literal(MsLoweringState* state,
                                         const MsAstNode* node) {
  size_t i;

  if (state == NULL || node == NULL || node->as.list.elements.count > UINT8_MAX) {
    return 0;
  }

  for (i = 0; i < node->as.list.elements.count; ++i) {
    if (!ms_lowering_emit_expression(state, node->as.list.elements.items[i])) {
      return 0;
    }
  }

  return ms_lowering_emit_opcode(state, MS_OP_BUILD_LIST, node->line) &&
         ms_lowering_emit_byte(state,
                               (uint8_t) node->as.list.elements.count,
                               node->line);
}

static int ms_lowering_emit_tuple_literal(MsLoweringState* state,
                                          const MsAstNode* node) {
  size_t i;

  if (state == NULL || node == NULL || node->as.tuple.elements.count > UINT8_MAX) {
    return 0;
  }

  for (i = 0; i < node->as.tuple.elements.count; ++i) {
    if (!ms_lowering_emit_expression(state, node->as.tuple.elements.items[i])) {
      return 0;
    }
  }

  return ms_lowering_emit_opcode(state, MS_OP_BUILD_TUPLE, node->line) &&
         ms_lowering_emit_byte(state,
                               (uint8_t) node->as.tuple.elements.count,
                               node->line);
}

static int ms_lowering_emit_map_literal(MsLoweringState* state,
                                        const MsAstNode* node) {
  size_t i;

  if (state == NULL || node == NULL || node->as.map.entries.count > UINT8_MAX) {
    return 0;
  }

  for (i = 0; i < node->as.map.entries.count; ++i) {
    if (!ms_lowering_emit_expression(state, node->as.map.entries.items[i].key) ||
        !ms_lowering_emit_expression(state, node->as.map.entries.items[i].value)) {
      return 0;
    }
  }

  return ms_lowering_emit_opcode(state, MS_OP_BUILD_MAP, node->line) &&
         ms_lowering_emit_byte(state,
                               (uint8_t) node->as.map.entries.count,
                               node->line);
}

static int ms_lowering_compile_function(MsLoweringState* state,
                                        size_t node_id,
                                        MsToken name,
                                        const MsTokenArray* parameters,
                                        const MsAstNode* body,
                                        MsFunction** out_function) {
  MsLoweringFunctionContext function_context;
  MsLoweringFunctionContext* enclosing;
  MsFunction* function = NULL;
  char* name_text = NULL;
  size_t name_length = 0;
  size_t i;
  int result = 0;

  if (state == NULL || parameters == NULL || body == NULL || out_function == NULL) {
    return 0;
  }

  if (name.start != NULL && name.length > 0 &&
      !ms_lowering_copy_token_text(name, 0, 0, &name_text, &name_length)) {
    return 0;
  }

  function = ms_function_new(name_text, name_length, (int) parameters->count);
  free(name_text);
  if (function == NULL) {
    return 0;
  }

  memset(&function_context, 0, sizeof(function_context));
  if (!ms_lowering_get_function_resolution(state,
                                           node_id,
                                           &function_context.resolution)) {
    ms_function_free(function);
    return 0;
  }

  function->upvalue_count = (uint8_t) function_context.resolution.upvalue_count;
  function->flags = function_context.resolution.flags;
  function_context.node_id = node_id;
  function_context.chunk = &function->chunk;
  function_context.scope_depth = 1;
  enclosing = state->current_function;
  function_context.enclosing = enclosing;
  state->current_function = &function_context;

  {
    uint8_t next_slot = 0;

    if ((function_context.resolution.flags & MS_FUNCTION_FLAG_HAS_SELF) != 0) {
      if (!ms_lowering_push_local(state,
                                  next_slot,
                                  1,
                                  ms_lowering_function_local_is_captured(
                                      &function_context,
                                      next_slot))) {
        goto cleanup;
      }
      next_slot += 1;
    }
    if ((function_context.resolution.flags & MS_FUNCTION_FLAG_HAS_SUPER) != 0) {
      if (!ms_lowering_push_local(state,
                                  next_slot,
                                  1,
                                  ms_lowering_function_local_is_captured(
                                      &function_context,
                                      next_slot))) {
        goto cleanup;
      }
      next_slot += 1;
    }
    for (i = 0; i < parameters->count; ++i) {
      uint8_t slot = (uint8_t) (next_slot + i);

      if (!ms_lowering_push_local(state,
                                  slot,
                                  1,
                                  ms_lowering_function_local_is_captured(
                                      &function_context,
                                      slot))) {
        goto cleanup;
      }
    }
  }

  if (!ms_lowering_emit_statement(state, body)) {
    goto cleanup;
  }
  if ((function_context.resolution.flags & MS_FUNCTION_FLAG_INITIALIZER) != 0) {
    if (!ms_lowering_emit_opcode(state, MS_OP_GET_LOCAL, body->line) ||
        !ms_lowering_emit_byte(state, 0, body->line) ||
        !ms_lowering_emit_opcode(state, MS_OP_RETURN, body->line)) {
      goto cleanup;
    }
  } else if (!ms_lowering_emit_opcode(state, MS_OP_NIL, body->line) ||
             !ms_lowering_emit_opcode(state, MS_OP_RETURN, body->line)) {
    goto cleanup;
  }

  result = 1;

cleanup:
  state->current_function = enclosing;
  free(function_context.locals);
  if (!result) {
    ms_function_free(function);
    return 0;
  }

  *out_function = function;
  return 1;
}

static int ms_lowering_emit_closure(MsLoweringState* state,
                                    const MsAstNode* node,
                                    MsFunction* function,
                                    size_t function_node_id) {
  MsChunk* chunk = ms_lowering_current_chunk(state);
  MsFunctionResolution resolution;
  uint8_t constant_index = 0;
  size_t i;

  if (chunk == NULL || state == NULL || node == NULL || function == NULL) {
    return 0;
  }
  if (!ms_lowering_get_function_resolution(state,
                                           function_node_id,
                                           &resolution) ||
      !ms_chunk_add_constant(chunk,
                             ms_value_object((MsObject*) function),
                             &constant_index) ||
      !ms_lowering_emit_opcode(state, MS_OP_CLOSURE, node->line) ||
      !ms_lowering_emit_byte(state, constant_index, node->line)) {
    return 0;
  }

  for (i = 0; i < resolution.upvalue_count; ++i) {
    if (!ms_lowering_emit_byte(state, resolution.upvalues[i].is_local, node->line) ||
        !ms_lowering_emit_byte(state, resolution.upvalues[i].slot, node->line)) {
      return 0;
    }
  }

  return 1;
}

static int ms_lowering_emit_expression(MsLoweringState* state,
                                       const MsAstNode* node) {
  size_t skip_right = 0;
  size_t end_jump = 0;
  size_t i;
  MsFunction* function = NULL;
  MsToken anonymous_name;

  if (state == NULL || node == NULL) {
    return 0;
  }

  switch (node->kind) {
    case MS_AST_LITERAL:
      return ms_lowering_emit_literal(state, node);
    case MS_AST_VARIABLE:
      return ms_lowering_emit_bound_get(state,
                                        node,
                                        node->as.variable.name);
    case MS_AST_SELF:
      return ms_lowering_emit_bound_get(state,
                                        node,
                                        node->as.self_expr.keyword);
    case MS_AST_SUPER:
      return ms_lowering_emit_bound_get(state,
                                        node,
                                        node->as.super_expr.keyword) &&
             ms_lowering_emit_named_opcode(state,
                                           MS_OP_GET_SUPER,
                                           node->as.super_expr.method,
                                           node->line);
    case MS_AST_UNARY:
      if (!ms_lowering_emit_expression(state, node->as.unary.operand)) {
        return 0;
      }
      if (node->as.unary.op.kind == MS_TOKEN_BANG) {
        return ms_lowering_emit_opcode(state, MS_OP_NOT, node->line);
      }
      if (node->as.unary.op.kind == MS_TOKEN_MINUS) {
        return ms_lowering_emit_opcode(state, MS_OP_NEGATE, node->line);
      }
      return 0;
    case MS_AST_BINARY:
      if (!ms_lowering_emit_expression(state, node->as.binary.left) ||
          !ms_lowering_emit_expression(state, node->as.binary.right)) {
        return 0;
      }
      switch (node->as.binary.op.kind) {
        case MS_TOKEN_PLUS:
          return ms_lowering_emit_opcode(state, MS_OP_ADD, node->line);
        case MS_TOKEN_MINUS:
          return ms_lowering_emit_opcode(state, MS_OP_SUBTRACT, node->line);
        case MS_TOKEN_STAR:
          return ms_lowering_emit_opcode(state, MS_OP_MULTIPLY, node->line);
        case MS_TOKEN_SLASH:
          return ms_lowering_emit_opcode(state, MS_OP_DIVIDE, node->line);
        case MS_TOKEN_EQUAL_EQUAL:
          return ms_lowering_emit_opcode(state, MS_OP_EQUAL, node->line);
        case MS_TOKEN_BANG_EQUAL:
          return ms_lowering_emit_opcode(state, MS_OP_EQUAL, node->line) &&
                 ms_lowering_emit_opcode(state, MS_OP_NOT, node->line);
        case MS_TOKEN_GREATER:
          return ms_lowering_emit_opcode(state, MS_OP_GREATER, node->line);
        case MS_TOKEN_LESS:
          return ms_lowering_emit_opcode(state, MS_OP_LESS, node->line);
        case MS_TOKEN_GREATER_EQUAL:
          return ms_lowering_emit_opcode(state, MS_OP_LESS, node->line) &&
                 ms_lowering_emit_opcode(state, MS_OP_NOT, node->line);
        case MS_TOKEN_LESS_EQUAL:
          return ms_lowering_emit_opcode(state, MS_OP_GREATER, node->line) &&
                 ms_lowering_emit_opcode(state, MS_OP_NOT, node->line);
        default:
          return 0;
      }
    case MS_AST_LOGICAL:
      if (!ms_lowering_emit_expression(state, node->as.logical.left)) {
        return 0;
      }
      if (node->as.logical.op.kind == MS_TOKEN_AND) {
        if (!ms_lowering_emit_jump(state,
                                   MS_OP_JUMP_IF_FALSE,
                                   node->line,
                                   &end_jump) ||
            !ms_lowering_emit_opcode(state, MS_OP_POP, node->line) ||
            !ms_lowering_emit_expression(state, node->as.logical.right)) {
          return 0;
        }
        return ms_lowering_patch_jump(state, end_jump);
      }
      if (!ms_lowering_emit_jump(state,
                                 MS_OP_JUMP_IF_FALSE,
                                 node->line,
                                 &skip_right) ||
          !ms_lowering_emit_jump(state,
                                 MS_OP_JUMP,
                                 node->line,
                                 &end_jump) ||
          !ms_lowering_patch_jump(state, skip_right) ||
          !ms_lowering_emit_opcode(state, MS_OP_POP, node->line) ||
          !ms_lowering_emit_expression(state, node->as.logical.right)) {
        return 0;
      }
      return ms_lowering_patch_jump(state, end_jump);
    case MS_AST_ASSIGN:
      if (node->as.assign.target == NULL) {
        return ms_lowering_append_diagnostic(state,
                                             node,
                                             "MS3005",
                                             "unsupported assignment target in basic lowering");
      }
      if (node->as.assign.target->kind == MS_AST_VARIABLE) {
        return ms_lowering_emit_expression(state, node->as.assign.value) &&
               ms_lowering_emit_bound_set(
                   state,
                   node->as.assign.target,
                   node->as.assign.target->as.variable.name);
      }
      if (node->as.assign.target->kind == MS_AST_PROPERTY) {
        return ms_lowering_emit_expression(state,
                                           node->as.assign.target->as.property.object) &&
               ms_lowering_emit_expression(state, node->as.assign.value) &&
               ms_lowering_emit_named_opcode(state,
                                             MS_OP_SET_PROPERTY,
                                             node->as.assign.target->as.property.name,
                                             node->line);
      }
      if (node->as.assign.target->kind == MS_AST_INDEX) {
        return ms_lowering_emit_expression(state,
                                           node->as.assign.target->as.index.object) &&
               ms_lowering_emit_expression(state,
                                           node->as.assign.target->as.index.index) &&
               ms_lowering_emit_expression(state, node->as.assign.value) &&
               ms_lowering_emit_opcode(state, MS_OP_INDEX_SET, node->line);
      }
      return ms_lowering_append_diagnostic(state,
                                           node,
                                           "MS3005",
                                           "unsupported assignment target in basic lowering");
    case MS_AST_CALL:
      if (!ms_lowering_emit_expression(state, node->as.call.callee)) {
        return 0;
      }
      if (node->as.call.arguments.count > UINT8_MAX) {
        return 0;
      }
      for (i = 0; i < node->as.call.arguments.count; ++i) {
        if (!ms_lowering_emit_expression(state, node->as.call.arguments.items[i])) {
          return 0;
        }
      }
      return ms_lowering_emit_opcode(state, MS_OP_CALL, node->line) &&
             ms_lowering_emit_byte(state,
                                   (uint8_t) node->as.call.arguments.count,
                                   node->line);
    case MS_AST_PROPERTY:
      return ms_lowering_emit_expression(state, node->as.property.object) &&
             ms_lowering_emit_named_opcode(state,
                                           MS_OP_GET_PROPERTY,
                                           node->as.property.name,
                                           node->line);
    case MS_AST_INDEX:
      return ms_lowering_emit_expression(state, node->as.index.object) &&
             ms_lowering_emit_expression(state, node->as.index.index) &&
             ms_lowering_emit_opcode(state, MS_OP_INDEX_GET, node->line);
    case MS_AST_LIST:
      return ms_lowering_emit_list_literal(state, node);
    case MS_AST_TUPLE:
      return ms_lowering_emit_tuple_literal(state, node);
    case MS_AST_MAP:
      return ms_lowering_emit_map_literal(state, node);
    case MS_AST_FUNCTION:
      memset(&anonymous_name, 0, sizeof(anonymous_name));
      if (!ms_lowering_compile_function(state,
                                        node->node_id,
                                        anonymous_name,
                                        &node->as.function.parameters,
                                        node->as.function.body,
                                        &function)) {
        return 0;
      }
      return ms_lowering_emit_closure(state, node, function, node->node_id);
    default:
      return ms_lowering_append_diagnostic(state,
                                           node,
                                           "MS3005",
                                           "unsupported feature in basic lowering");
  }
}
static int ms_lowering_emit_if(MsLoweringState* state,
                               const MsAstNode* node) {
  size_t then_jump = 0;
  size_t else_jump = 0;
  size_t end_jump = 0;

  if (!ms_lowering_emit_expression(state, node->as.if_stmt.condition) ||
      !ms_lowering_emit_jump(state,
                             MS_OP_JUMP_IF_FALSE,
                             node->line,
                             &then_jump) ||
      !ms_lowering_emit_opcode(state, MS_OP_POP, node->line) ||
      !ms_lowering_emit_statement(state, node->as.if_stmt.then_branch)) {
    return 0;
  }

  if (node->as.if_stmt.else_branch != NULL) {
    if (!ms_lowering_emit_jump(state,
                               MS_OP_JUMP,
                               node->line,
                               &else_jump) ||
        !ms_lowering_patch_jump(state, then_jump) ||
        !ms_lowering_emit_opcode(state, MS_OP_POP, node->line) ||
        !ms_lowering_emit_statement(state, node->as.if_stmt.else_branch)) {
      return 0;
    }
    return ms_lowering_patch_jump(state, else_jump);
  }

  return ms_lowering_emit_jump(state,
                               MS_OP_JUMP,
                               node->line,
                               &end_jump) &&
         ms_lowering_patch_jump(state, then_jump) &&
         ms_lowering_emit_opcode(state, MS_OP_POP, node->line) &&
         ms_lowering_patch_jump(state, end_jump);
}

static int ms_lowering_emit_while(MsLoweringState* state,
                                  const MsAstNode* node) {
  MsChunk* chunk = ms_lowering_current_chunk(state);
  size_t loop_start;
  size_t exit_jump = 0;
  MsLoopContext loop;
  MsLoopContext* enclosing_loop;

  if (chunk == NULL) {
    return 0;
  }

  loop_start = ms_chunk_code_count(chunk);
  memset(&loop, 0, sizeof(loop));
  loop.continue_target = loop_start;
  loop.continue_scope_depth = state->current_function->scope_depth;
  loop.break_scope_depth = state->current_function->scope_depth;
  enclosing_loop = state->current_function->current_loop;
  loop.enclosing = enclosing_loop;

  if (!ms_lowering_emit_expression(state, node->as.while_stmt.condition) ||
      !ms_lowering_emit_jump(state,
                             MS_OP_JUMP_IF_FALSE,
                             node->line,
                             &exit_jump) ||
      !ms_lowering_emit_opcode(state, MS_OP_POP, node->line)) {
    return 0;
  }

  state->current_function->current_loop = &loop;
  if (!ms_lowering_emit_statement(state, node->as.while_stmt.body)) {
    state->current_function->current_loop = enclosing_loop;
    free(loop.break_patches);
    return 0;
  }
  state->current_function->current_loop = enclosing_loop;

  if (!ms_lowering_emit_loop(state, loop_start, node->line) ||
      !ms_lowering_patch_jump(state, exit_jump) ||
      !ms_lowering_emit_opcode(state, MS_OP_POP, node->line) ||
      !ms_lowering_patch_breaks(state, &loop)) {
    free(loop.break_patches);
    return 0;
  }

  free(loop.break_patches);
  return 1;
}

static int ms_lowering_emit_for(MsLoweringState* state,
                                const MsAstNode* node) {
  MsChunk* chunk = ms_lowering_current_chunk(state);
  size_t loop_start;
  size_t continue_target;
  size_t exit_jump = 0;
  size_t body_jump = 0;
  int has_exit_jump = 0;
  MsLoopContext loop;
  MsLoopContext* enclosing_loop;

  if (chunk == NULL || state == NULL || node == NULL) {
    return 0;
  }

  if (!ms_lowering_begin_scope(state)) {
    return 0;
  }
  if (node->as.for_stmt.initializer != NULL &&
      !ms_lowering_emit_statement(state, node->as.for_stmt.initializer)) {
    return 0;
  }

  loop_start = ms_chunk_code_count(chunk);
  if (node->as.for_stmt.condition != NULL) {
    if (!ms_lowering_emit_expression(state, node->as.for_stmt.condition) ||
        !ms_lowering_emit_jump(state,
                               MS_OP_JUMP_IF_FALSE,
                               node->line,
                               &exit_jump) ||
        !ms_lowering_emit_opcode(state, MS_OP_POP, node->line)) {
      return 0;
    }
    has_exit_jump = 1;
  }

  continue_target = loop_start;
  if (node->as.for_stmt.increment != NULL) {
    if (!ms_lowering_emit_jump(state,
                               MS_OP_JUMP,
                               node->line,
                               &body_jump)) {
      return 0;
    }
    continue_target = ms_chunk_code_count(chunk);
    if (!ms_lowering_emit_expression(state, node->as.for_stmt.increment) ||
        !ms_lowering_emit_opcode(state, MS_OP_POP, node->line) ||
        !ms_lowering_emit_loop(state, loop_start, node->line) ||
        !ms_lowering_patch_jump(state, body_jump)) {
      return 0;
    }
    loop_start = continue_target;
  }

  memset(&loop, 0, sizeof(loop));
  loop.continue_target = continue_target;
  loop.continue_scope_depth = state->current_function->scope_depth;
  loop.break_scope_depth = state->current_function->scope_depth;
  enclosing_loop = state->current_function->current_loop;
  loop.enclosing = enclosing_loop;
  state->current_function->current_loop = &loop;

  if (!ms_lowering_emit_statement(state, node->as.for_stmt.body)) {
    state->current_function->current_loop = enclosing_loop;
    free(loop.break_patches);
    return 0;
  }
  state->current_function->current_loop = enclosing_loop;

  if (!ms_lowering_emit_loop(state, loop_start, node->line)) {
    free(loop.break_patches);
    return 0;
  }
  if (has_exit_jump) {
    if (!ms_lowering_patch_jump(state, exit_jump) ||
        !ms_lowering_emit_opcode(state, MS_OP_POP, node->line)) {
      free(loop.break_patches);
      return 0;
    }
  }
  if (!ms_lowering_patch_breaks(state, &loop)) {
    free(loop.break_patches);
    return 0;
  }

  free(loop.break_patches);
  return ms_lowering_end_scope(state, node->line);
}

static int ms_lowering_emit_statement(MsLoweringState* state,
                                      const MsAstNode* node) {
  MsResolvedBinding binding;
  uint8_t name_index = 0;
  size_t i;
  MsFunction* function = NULL;

  if (state == NULL || node == NULL) {
    return 0;
  }

  switch (node->kind) {
    case MS_AST_PROGRAM:
      for (i = 0; i < node->as.program.declarations.count; ++i) {
        if (!ms_lowering_emit_statement(state,
                                        node->as.program.declarations.items[i])) {
          return 0;
        }
      }
      return 1;
    case MS_AST_BLOCK:
      if (!ms_lowering_begin_scope(state)) {
        return 0;
      }
      for (i = 0; i < node->as.block.statements.count; ++i) {
        if (!ms_lowering_emit_statement(state,
                                        node->as.block.statements.items[i])) {
          return 0;
        }
      }
      return ms_lowering_end_scope(state, node->line);
    case MS_AST_VAR_DECL:
      if (node->as.var_decl.initializer != NULL) {
        if (!ms_lowering_emit_expression(state, node->as.var_decl.initializer)) {
          return 0;
        }
      } else if (!ms_lowering_emit_opcode(state, MS_OP_NIL, node->line)) {
        return 0;
      }
      if (!ms_lowering_get_binding(state, node, &binding)) {
        return 0;
      }
      if (binding.kind == MS_BINDING_LOCAL) {
        return ms_lowering_push_local(state,
                                      binding.slot,
                                      binding.scope_depth,
                                      binding.is_captured);
      }
      if (!ms_lowering_emit_name_operand(state,
                                         node->as.var_decl.name,
                                         node->line,
                                         &name_index) ||
          !ms_lowering_emit_opcode(state, MS_OP_DEFINE_GLOBAL, node->line)) {
        return 0;
      }
      return ms_lowering_emit_byte(state, name_index, node->line);
    case MS_AST_FUNCTION_DECL:
      if (!ms_lowering_compile_function(state,
                                        node->node_id,
                                        node->as.function_decl.name,
                                        &node->as.function_decl.parameters,
                                        node->as.function_decl.body,
                                        &function) ||
          !ms_lowering_get_binding(state, node, &binding)) {
        return 0;
      }
      if (binding.kind == MS_BINDING_LOCAL) {
        if (!ms_lowering_emit_opcode(state, MS_OP_NIL, node->line) ||
            !ms_lowering_push_local(state,
                                    binding.slot,
                                    binding.scope_depth,
                                    binding.is_captured) ||
            !ms_lowering_emit_closure(state, node, function, node->node_id) ||
            !ms_lowering_emit_opcode(state, MS_OP_SET_LOCAL, node->line) ||
            !ms_lowering_emit_byte(state, binding.slot, node->line) ||
            !ms_lowering_emit_opcode(state, MS_OP_POP, node->line)) {
          return 0;
        }
        return 1;
      }
      if (!ms_lowering_emit_closure(state, node, function, node->node_id) ||
          !ms_lowering_emit_name_operand(state,
                                         node->as.function_decl.name,
                                         node->line,
                                         &name_index) ||
          !ms_lowering_emit_opcode(state, MS_OP_DEFINE_GLOBAL, node->line)) {
        return 0;
      }
      return ms_lowering_emit_byte(state, name_index, node->line);
    case MS_AST_CLASS_DECL:
      return ms_lowering_emit_class_decl(state, node);
    case MS_AST_IMPORT_STMT:
      return ms_lowering_emit_import_module_stmt(state, node);
    case MS_AST_FROM_IMPORT_STMT:
      return ms_lowering_emit_from_import_stmt(state, node);
    case MS_AST_EXPR_STMT:
      return ms_lowering_emit_expression(state, node->as.expr_stmt.expression) &&
             ms_lowering_emit_opcode(state, MS_OP_POP, node->line);
    case MS_AST_PRINT_STMT:
      return ms_lowering_emit_expression(state, node->as.print_stmt.expression) &&
             ms_lowering_emit_opcode(state, MS_OP_PRINT, node->line);
    case MS_AST_RETURN_STMT:
      if (node->as.return_stmt.value != NULL) {
        return ms_lowering_emit_expression(state, node->as.return_stmt.value) &&
               ms_lowering_emit_opcode(state, MS_OP_RETURN, node->line);
      }
      if ((state->current_function->resolution.flags &
           MS_FUNCTION_FLAG_INITIALIZER) != 0) {
        return ms_lowering_emit_opcode(state, MS_OP_GET_LOCAL, node->line) &&
               ms_lowering_emit_byte(state, 0, node->line) &&
               ms_lowering_emit_opcode(state, MS_OP_RETURN, node->line);
      }
      return ms_lowering_emit_opcode(state, MS_OP_NIL, node->line) &&
             ms_lowering_emit_opcode(state, MS_OP_RETURN, node->line);
    case MS_AST_IF_STMT:
      return ms_lowering_emit_if(state, node);
    case MS_AST_WHILE_STMT:
      return ms_lowering_emit_while(state, node);
    case MS_AST_FOR_STMT:
      return ms_lowering_emit_for(state, node);
    case MS_AST_BREAK_STMT:
      return ms_lowering_emit_break(state, node);
    case MS_AST_CONTINUE_STMT:
      return ms_lowering_emit_continue(state, node);
    default:
      return ms_lowering_append_diagnostic(state,
                                           node,
                                           "MS3005",
                                           "unsupported feature in basic lowering");
  }
}
int ms_lower_program(const char* file,
                     const MsAstNode* program,
                     const MsResolutionTable* table,
                     MsChunk* chunk,
                     MsDiagnosticList* diagnostics) {
  MsLoweringState state;
  MsLoweringFunctionContext top_level;

  if (program == NULL || table == NULL || chunk == NULL || diagnostics == NULL) {
    return 0;
  }

  memset(&state, 0, sizeof(state));
  memset(&top_level, 0, sizeof(top_level));
  if (!ms_resolution_table_get_function(table, 0, &top_level.resolution)) {
    return 0;
  }

  top_level.node_id = 0;
  top_level.chunk = chunk;
  top_level.scope_depth = 0;
  state.file = file != NULL ? file : "<unknown>";
  state.resolution = table;
  state.diagnostics = diagnostics;
  state.current_function = &top_level;

  if (!ms_lowering_emit_statement(&state, program) ||
      !ms_lowering_emit_opcode(&state, MS_OP_NIL, program->line) ||
      !ms_lowering_emit_opcode(&state, MS_OP_RETURN, program->line)) {
    free(top_level.locals);
    return 0;
  }

  free(top_level.locals);
  return 1;
}

MsCompileResult ms_compile_source(const char* file,
                                  const char* source,
                                  MsChunk* chunk,
                                  MsDiagnosticList* diagnostics) {
  MsArena arena;
  MsAstNode* program = NULL;
  MsResolutionTable table;
  MsCompileResult result = MS_COMPILE_RESULT_LOWER_ERROR;

  if (chunk == NULL || diagnostics == NULL) {
    return MS_COMPILE_RESULT_LOWER_ERROR;
  }

  ms_diag_list_clear(diagnostics);
  ms_arena_init(&arena, 8192);
  ms_resolution_table_init(&table);

  program = ms_parse_source(file, source, &arena, diagnostics);
  if (ms_diag_list_count(diagnostics) > 0 || program == NULL) {
    result = MS_COMPILE_RESULT_PARSE_ERROR;
    goto cleanup;
  }

  if (!ms_resolve_program(file, program, &table, diagnostics) ||
      ms_diag_list_count(diagnostics) > 0) {
    result = MS_COMPILE_RESULT_RESOLVE_ERROR;
    goto cleanup;
  }

  if (!ms_lower_program(file, program, &table, chunk, diagnostics)) {
    result = ms_diag_list_count(diagnostics) > 0 ?
        MS_COMPILE_RESULT_RESOLVE_ERROR :
        MS_COMPILE_RESULT_LOWER_ERROR;
    goto cleanup;
  }

  result = MS_COMPILE_RESULT_OK;

cleanup:
  ms_resolution_table_destroy(&table);
  ms_arena_destroy(&arena);
  return result;
}
