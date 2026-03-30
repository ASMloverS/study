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
#include "ms/runtime/opcode.h"
#include "ms/string.h"
#include "ms/value.h"

typedef struct MsActiveLocal {
  uint8_t slot;
  int scope_depth;
} MsActiveLocal;

typedef struct MsLoopContext {
  size_t continue_target;
  int continue_scope_depth;
  int break_scope_depth;
  size_t *break_patches;
  size_t break_count;
  size_t break_capacity;
  struct MsLoopContext *enclosing;
} MsLoopContext;

typedef struct MsLoweringState {
  const char *file;
  const MsResolutionTable *resolution;
  MsChunk *chunk;
  MsDiagnosticList *diagnostics;
  MsActiveLocal *locals;
  size_t local_count;
  size_t local_capacity;
  int scope_depth;
  MsLoopContext *current_loop;
} MsLoweringState;

static int ms_lowering_append_diagnostic(MsLoweringState *state,
                                         const MsAstNode *node,
                                         const char *code,
                                         const char *message) {
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

static int ms_lowering_ensure_locals(MsLoweringState *state, size_t min_capacity) {
  MsActiveLocal *locals;
  size_t new_capacity;

  if (state == NULL) {
    return 0;
  }
  if (min_capacity <= state->local_capacity) {
    return 1;
  }

  new_capacity = state->local_capacity == 0 ? 16 : state->local_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  locals = (MsActiveLocal *) realloc(state->locals, new_capacity * sizeof(*locals));
  if (locals == NULL) {
    return 0;
  }

  state->locals = locals;
  state->local_capacity = new_capacity;
  return 1;
}

static int ms_loop_ensure_breaks(MsLoopContext *loop, size_t min_capacity) {
  size_t *patches;
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

  patches = (size_t *) realloc(loop->break_patches,
                               new_capacity * sizeof(*patches));
  if (patches == NULL) {
    return 0;
  }

  loop->break_patches = patches;
  loop->break_capacity = new_capacity;
  return 1;
}

static int ms_lowering_emit_byte(MsLoweringState *state,
                                 uint8_t byte,
                                 int line) {
  return state != NULL && ms_chunk_write(state->chunk, byte, line);
}

static int ms_lowering_emit_opcode(MsLoweringState *state,
                                   MsOpcode opcode,
                                   int line) {
  return ms_lowering_emit_byte(state, (uint8_t) opcode, line);
}

static int ms_lowering_emit_short(MsLoweringState *state,
                                  uint16_t value,
                                  int line) {
  return state != NULL && ms_chunk_write_short(state->chunk, value, line);
}

static int ms_lowering_emit_jump(MsLoweringState *state,
                                 MsOpcode opcode,
                                 int line,
                                 size_t *out_patch_offset) {
  if (state == NULL || out_patch_offset == NULL ||
      !ms_lowering_emit_opcode(state, opcode, line)) {
    return 0;
  }

  *out_patch_offset = ms_chunk_code_count(state->chunk);
  return ms_lowering_emit_short(state, 0xffffu, line);
}

static int ms_lowering_patch_jump(MsLoweringState *state, size_t patch_offset) {
  size_t code_count;
  size_t distance;

  if (state == NULL) {
    return 0;
  }

  code_count = ms_chunk_code_count(state->chunk);
  if (code_count < patch_offset + 2) {
    return 0;
  }

  distance = code_count - patch_offset - 2;
  if (distance > UINT16_MAX) {
    return 0;
  }

  return ms_chunk_patch_short(state->chunk,
                              patch_offset,
                              (uint16_t) distance);
}

static int ms_lowering_emit_loop(MsLoweringState *state,
                                 size_t target,
                                 int line) {
  size_t distance;

  if (state == NULL || !ms_lowering_emit_opcode(state, MS_OP_LOOP, line)) {
    return 0;
  }

  distance = ms_chunk_code_count(state->chunk) + 2 - target;
  if (distance > UINT16_MAX) {
    return 0;
  }

  return ms_lowering_emit_short(state, (uint16_t) distance, line);
}

static int ms_lowering_copy_token_text(MsToken token,
                                       size_t trim_left,
                                       size_t trim_right,
                                       char **out_text,
                                       size_t *out_length) {
  char *text;
  size_t length;

  if (out_text == NULL || out_length == NULL ||
      token.length < trim_left + trim_right) {
    return 0;
  }

  length = token.length - trim_left - trim_right;
  text = (char *) malloc(length + 1);
  if (text == NULL) {
    return 0;
  }

  memcpy(text, token.start + trim_left, length);
  text[length] = '\0';
  *out_text = text;
  *out_length = length;
  return 1;
}

static int ms_lowering_emit_constant_value(MsLoweringState *state,
                                           const MsAstNode *node,
                                           MsValue value) {
  uint8_t constant_index = 0;

  if (state == NULL || node == NULL ||
      !ms_chunk_add_constant(state->chunk, value, &constant_index) ||
      !ms_lowering_emit_opcode(state, MS_OP_CONSTANT, node->line)) {
    return 0;
  }

  return ms_lowering_emit_byte(state, constant_index, node->line);
}

static int ms_lowering_emit_name_operand(MsLoweringState *state,
                                         MsToken token,
                                         int line,
                                         uint8_t *out_index) {
  char *text = NULL;
  size_t length = 0;
  MsString *string = NULL;

  (void) line;
  if (state == NULL || out_index == NULL ||
      !ms_lowering_copy_token_text(token, 0, 0, &text, &length)) {
    return 0;
  }

  string = ms_string_new(text, length);
  free(text);
  if (string == NULL) {
    return 0;
  }

  if (!ms_chunk_add_constant(state->chunk,
                             ms_value_object((MsObject *) string),
                             out_index)) {
    return 0;
  }
  return 1;
}

static int ms_lowering_emit_literal(MsLoweringState *state,
                                    const MsAstNode *node) {
  char *text = NULL;
  size_t length = 0;
  char *end = NULL;
  double number = 0.0;
  MsString *string = NULL;

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
                                             ms_value_object((MsObject *) string));
    default:
      return 0;
  }
}

static int ms_lowering_push_local(MsLoweringState *state,
                                  uint8_t slot,
                                  int scope_depth) {
  if (state == NULL ||
      !ms_lowering_ensure_locals(state, state->local_count + 1)) {
    return 0;
  }

  state->locals[state->local_count].slot = slot;
  state->locals[state->local_count].scope_depth = scope_depth;
  state->local_count += 1;
  return 1;
}

static int ms_lowering_begin_scope(MsLoweringState *state) {
  if (state == NULL) {
    return 0;
  }

  state->scope_depth += 1;
  return 1;
}

static int ms_lowering_pop_locals_to_depth(MsLoweringState *state,
                                           int target_depth,
                                           int line) {
  size_t i;

  if (state == NULL) {
    return 0;
  }

  for (i = state->local_count; i > 0; --i) {
    if (state->locals[i - 1].scope_depth <= target_depth) {
      break;
    }
    if (!ms_lowering_emit_opcode(state, MS_OP_POP, line)) {
      return 0;
    }
  }

  return 1;
}

static int ms_lowering_end_scope(MsLoweringState *state, int line) {
  if (state == NULL) {
    return 0;
  }

  while (state->local_count > 0 &&
         state->locals[state->local_count - 1].scope_depth == state->scope_depth) {
    if (!ms_lowering_emit_opcode(state, MS_OP_POP, line)) {
      return 0;
    }
    state->local_count -= 1;
  }

  state->scope_depth -= 1;
  return 1;
}

static int ms_lowering_get_binding(MsLoweringState *state,
                                   const MsAstNode *node,
                                   MsResolvedBinding *out_binding) {
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

static int ms_lowering_emit_bound_name(MsLoweringState *state,
                                       const MsAstNode *node,
                                       MsOpcode local_opcode,
                                       MsOpcode global_opcode,
                                       MsToken name_token) {
  MsResolvedBinding binding;
  uint8_t name_index = 0;

  if (!ms_lowering_get_binding(state, node, &binding)) {
    return 0;
  }

  if (binding.kind == MS_BINDING_LOCAL) {
    return ms_lowering_emit_opcode(state, local_opcode, node->line) &&
           ms_lowering_emit_byte(state, binding.slot, node->line);
  }

  if (!ms_lowering_emit_name_operand(state, name_token, node->line, &name_index)) {
    return 0;
  }
  return ms_lowering_emit_opcode(state, global_opcode, node->line) &&
         ms_lowering_emit_byte(state, name_index, node->line);
}

static int ms_lowering_emit_break(MsLoweringState *state, const MsAstNode *node) {
  size_t patch_offset = 0;
  MsLoopContext *loop = state != NULL ? state->current_loop : NULL;

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

static int ms_lowering_emit_continue(MsLoweringState *state,
                                     const MsAstNode *node) {
  MsLoopContext *loop = state != NULL ? state->current_loop : NULL;

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

static int ms_lowering_patch_breaks(MsLoweringState *state, MsLoopContext *loop) {
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

static int ms_lowering_emit_expression(MsLoweringState *state,
                                       const MsAstNode *node);
static int ms_lowering_emit_statement(MsLoweringState *state,
                                      const MsAstNode *node);

static int ms_lowering_emit_expression(MsLoweringState *state,
                                       const MsAstNode *node) {
  size_t skip_right = 0;
  size_t end_jump = 0;

  if (state == NULL || node == NULL) {
    return 0;
  }

  switch (node->kind) {
    case MS_AST_LITERAL:
      return ms_lowering_emit_literal(state, node);
    case MS_AST_VARIABLE:
      return ms_lowering_emit_bound_name(state,
                                         node,
                                         MS_OP_GET_LOCAL,
                                         MS_OP_GET_GLOBAL,
                                         node->as.variable.name);
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
      if (node->as.assign.target == NULL ||
          node->as.assign.target->kind != MS_AST_VARIABLE) {
        return ms_lowering_append_diagnostic(state,
                                             node,
                                             "MS3005",
                                             "unsupported assignment target in basic lowering");
      }
      return ms_lowering_emit_expression(state, node->as.assign.value) &&
             ms_lowering_emit_bound_name(state,
                                         node->as.assign.target,
                                         MS_OP_SET_LOCAL,
                                         MS_OP_SET_GLOBAL,
                                         node->as.assign.target->as.variable.name);
    default:
      return ms_lowering_append_diagnostic(state,
                                           node,
                                           "MS3005",
                                           "unsupported feature in basic lowering");
  }
}

static int ms_lowering_emit_if(MsLoweringState *state,
                               const MsAstNode *node) {
  size_t then_jump = 0;
  size_t else_jump = 0;

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

  return ms_lowering_patch_jump(state, then_jump) &&
         ms_lowering_emit_opcode(state, MS_OP_POP, node->line);
}

static int ms_lowering_emit_while(MsLoweringState *state,
                                  const MsAstNode *node) {
  size_t loop_start = ms_chunk_code_count(state->chunk);
  size_t exit_jump = 0;
  MsLoopContext loop;

  memset(&loop, 0, sizeof(loop));
  loop.continue_target = loop_start;
  loop.continue_scope_depth = state->scope_depth;
  loop.break_scope_depth = state->scope_depth;
  loop.enclosing = state->current_loop;

  if (!ms_lowering_emit_expression(state, node->as.while_stmt.condition) ||
      !ms_lowering_emit_jump(state,
                             MS_OP_JUMP_IF_FALSE,
                             node->line,
                             &exit_jump) ||
      !ms_lowering_emit_opcode(state, MS_OP_POP, node->line)) {
    return 0;
  }

  state->current_loop = &loop;
  if (!ms_lowering_emit_statement(state, node->as.while_stmt.body)) {
    state->current_loop = loop.enclosing;
    free(loop.break_patches);
    return 0;
  }
  state->current_loop = loop.enclosing;

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

static int ms_lowering_emit_for(MsLoweringState *state,
                                const MsAstNode *node) {
  size_t outer_depth;
  size_t loop_start;
  size_t continue_target;
  size_t exit_jump = 0;
  size_t body_jump = 0;
  int has_exit_jump = 0;
  MsLoopContext loop;

  if (state == NULL || node == NULL) {
    return 0;
  }

  outer_depth = (size_t) state->scope_depth;
  if (!ms_lowering_begin_scope(state)) {
    return 0;
  }
  if (node->as.for_stmt.initializer != NULL &&
      !ms_lowering_emit_statement(state, node->as.for_stmt.initializer)) {
    return 0;
  }

  loop_start = ms_chunk_code_count(state->chunk);
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
    continue_target = ms_chunk_code_count(state->chunk);
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
  loop.continue_scope_depth = state->scope_depth;
  loop.break_scope_depth = state->scope_depth;
  loop.enclosing = state->current_loop;
  state->current_loop = &loop;

  if (!ms_lowering_emit_statement(state, node->as.for_stmt.body)) {
    state->current_loop = loop.enclosing;
    free(loop.break_patches);
    return 0;
  }
  state->current_loop = loop.enclosing;

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

static int ms_lowering_emit_statement(MsLoweringState *state,
                                      const MsAstNode *node) {
  MsResolvedBinding binding;
  uint8_t name_index = 0;
  size_t i;

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
      if (!ms_lowering_emit_expression(state, node->as.var_decl.initializer) ||
          !ms_lowering_get_binding(state, node, &binding)) {
        return 0;
      }
      if (binding.kind == MS_BINDING_LOCAL) {
        return ms_lowering_push_local(state, binding.slot, state->scope_depth);
      }
      if (!ms_lowering_emit_name_operand(state,
                                         node->as.var_decl.name,
                                         node->line,
                                         &name_index) ||
          !ms_lowering_emit_opcode(state, MS_OP_DEFINE_GLOBAL, node->line)) {
        return 0;
      }
      return ms_lowering_emit_byte(state, name_index, node->line);
    case MS_AST_EXPR_STMT:
      return ms_lowering_emit_expression(state, node->as.expr_stmt.expression) &&
             ms_lowering_emit_opcode(state, MS_OP_POP, node->line);
    case MS_AST_PRINT_STMT:
      return ms_lowering_emit_expression(state, node->as.print_stmt.expression) &&
             ms_lowering_emit_opcode(state, MS_OP_PRINT, node->line);
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

int ms_lower_program(const char *file,
                     const MsAstNode *program,
                     const MsResolutionTable *table,
                     MsChunk *chunk,
                     MsDiagnosticList *diagnostics) {
  MsLoweringState state;

  if (program == NULL || table == NULL || chunk == NULL || diagnostics == NULL) {
    return 0;
  }

  state.file = file != NULL ? file : "<unknown>";
  state.resolution = table;
  state.chunk = chunk;
  state.diagnostics = diagnostics;
  state.locals = NULL;
  state.local_count = 0;
  state.local_capacity = 0;
  state.scope_depth = 0;
  state.current_loop = NULL;

  if (!ms_lowering_emit_statement(&state, program) ||
      !ms_lowering_emit_opcode(&state, MS_OP_RETURN, program->line)) {
    free(state.locals);
    return 0;
  }

  free(state.locals);
  return 1;
}

MsCompileResult ms_compile_source(const char *file,
                                  const char *source,
                                  MsChunk *chunk,
                                  MsDiagnosticList *diagnostics) {
  MsArena arena;
  MsAstNode *program = NULL;
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