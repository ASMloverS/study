#include "ms/frontend/resolver.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum MsLookupResult {
  MS_LOOKUP_ERROR = -1,
  MS_LOOKUP_NOT_FOUND = 0,
  MS_LOOKUP_FOUND = 1
} MsLookupResult;

typedef struct MsResolverLocal {
  MsToken name;
  size_t binding_node_id;
  int scope_depth;
  int initialized;
  uint8_t slot;
} MsResolverLocal;

typedef struct MsResolverGlobal {
  MsToken name;
  size_t binding_node_id;
  int initialized;
} MsResolverGlobal;

typedef struct MsResolverFunctionContext {
  size_t node_id;
  int is_function;
  unsigned flags;
  MsResolverLocal *locals;
  size_t local_count;
  size_t max_local_count;
  size_t local_capacity;
  size_t *scope_markers;
  size_t scope_count;
  size_t scope_capacity;
  int scope_depth;
  int loop_depth;
  struct MsResolverFunctionContext *enclosing;
} MsResolverFunctionContext;

typedef struct MsResolver {
  const char *file;
  MsResolutionTable *table;
  MsDiagnosticList *diagnostics;
  MsResolverGlobal *globals;
  size_t global_count;
  size_t global_capacity;
  MsResolverFunctionContext *current_function;
  int had_error;
} MsResolver;

static int ms_token_equals(MsToken left, MsToken right) {
  return left.length == right.length &&
         memcmp(left.start, right.start, left.length) == 0;
}

static int ms_resolver_append_span_diagnostic(MsResolver *resolver,
                                              int line,
                                              int column,
                                              int length,
                                              const char *code,
                                              const char *message) {
  MsDiagnostic diagnostic;

  if (resolver == NULL || resolver->diagnostics == NULL) {
    return 0;
  }

  diagnostic.phase = "resolve";
  diagnostic.code = code;
  diagnostic.message = message;
  diagnostic.span.file = resolver->file;
  diagnostic.span.line = line;
  diagnostic.span.column = column;
  diagnostic.span.length = length;
  resolver->had_error = 1;
  return ms_diag_list_append(resolver->diagnostics, &diagnostic);
}

static int ms_resolver_append_diagnostic(MsResolver *resolver,
                                         const MsAstNode *node,
                                         const char *code,
                                         const char *message) {
  if (resolver == NULL || node == NULL) {
    return 0;
  }

  return ms_resolver_append_span_diagnostic(resolver,
                                            node->line,
                                            node->column,
                                            node->end_column - node->column + 1,
                                            code,
                                            message);
}

static int ms_resolver_append_token_diagnostic(MsResolver *resolver,
                                               MsToken token,
                                               const char *code,
                                               const char *message) {
  return ms_resolver_append_span_diagnostic(resolver,
                                            token.line,
                                            token.column,
                                            token.end_column - token.column + 1,
                                            code,
                                            message);
}

static int ms_resolver_ensure_globals(MsResolver *resolver, size_t min_capacity) {
  MsResolverGlobal *globals;
  size_t new_capacity;

  if (resolver == NULL) {
    return 0;
  }
  if (min_capacity <= resolver->global_capacity) {
    return 1;
  }

  new_capacity = resolver->global_capacity == 0 ? 16 : resolver->global_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  globals = (MsResolverGlobal *) realloc(resolver->globals,
                                         new_capacity * sizeof(*globals));
  if (globals == NULL) {
    return 0;
  }

  resolver->globals = globals;
  resolver->global_capacity = new_capacity;
  return 1;
}

static int ms_resolver_ensure_locals(MsResolverFunctionContext *function,
                                     size_t min_capacity) {
  MsResolverLocal *locals;
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

  locals = (MsResolverLocal *) realloc(function->locals,
                                       new_capacity * sizeof(*locals));
  if (locals == NULL) {
    return 0;
  }

  function->locals = locals;
  function->local_capacity = new_capacity;
  return 1;
}

static int ms_resolver_ensure_scope_markers(MsResolverFunctionContext *function,
                                            size_t min_capacity) {
  size_t *markers;
  size_t new_capacity;

  if (function == NULL) {
    return 0;
  }
  if (min_capacity <= function->scope_capacity) {
    return 1;
  }

  new_capacity = function->scope_capacity == 0 ? 8 : function->scope_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  markers = (size_t *) realloc(function->scope_markers,
                               new_capacity * sizeof(*markers));
  if (markers == NULL) {
    return 0;
  }

  function->scope_markers = markers;
  function->scope_capacity = new_capacity;
  return 1;
}

static int ms_resolver_begin_scope(MsResolverFunctionContext *function) {
  if (function == NULL ||
      !ms_resolver_ensure_scope_markers(function, function->scope_count + 1)) {
    return 0;
  }

  function->scope_markers[function->scope_count] = function->local_count;
  function->scope_count += 1;
  function->scope_depth += 1;
  return 1;
}

static int ms_resolver_end_scope(MsResolverFunctionContext *function) {
  if (function == NULL || function->scope_count == 0) {
    return 0;
  }

  function->scope_count -= 1;
  function->local_count = function->scope_markers[function->scope_count];
  function->scope_depth -= 1;
  return 1;
}

static int ms_resolver_local_scope_start(const MsResolverFunctionContext *function,
                                         size_t *out_start) {
  if (function == NULL || out_start == NULL) {
    return 0;
  }

  if (function->scope_count == 0) {
    *out_start = 0;
  } else {
    *out_start = function->scope_markers[function->scope_count - 1];
  }
  return 1;
}

static int ms_resolver_declare_local(MsResolver *resolver,
                                     MsResolverFunctionContext *function,
                                     const MsAstNode *node,
                                     MsToken name,
                                     size_t binding_node_id,
                                     uint8_t *out_slot) {
  size_t scope_start = 0;
  size_t i;

  if (resolver == NULL || function == NULL || out_slot == NULL) {
    return 0;
  }
  if (function->local_count > UCHAR_MAX) {
    return node != NULL ?
        ms_resolver_append_diagnostic(resolver,
                                      node,
                                      "MS3006",
                                      "too many local variables in a function") :
        ms_resolver_append_token_diagnostic(resolver,
                                            name,
                                            "MS3006",
                                            "too many local variables in a function");
  }
  if (!ms_resolver_local_scope_start(function, &scope_start)) {
    return 0;
  }

  for (i = function->local_count; i > scope_start; --i) {
    if (ms_token_equals(function->locals[i - 1].name, name)) {
      return node != NULL ?
          ms_resolver_append_diagnostic(resolver,
                                        node,
                                        "MS3002",
                                        "duplicate declaration in the same scope") :
          ms_resolver_append_token_diagnostic(resolver,
                                              name,
                                              "MS3002",
                                              "duplicate declaration in the same scope");
    }
  }
  if (!ms_resolver_ensure_locals(function, function->local_count + 1)) {
    return 0;
  }

  function->locals[function->local_count].name = name;
  function->locals[function->local_count].binding_node_id = binding_node_id;
  function->locals[function->local_count].scope_depth = function->scope_depth;
  function->locals[function->local_count].initialized = 0;
  function->locals[function->local_count].slot = (uint8_t) function->local_count;
  *out_slot = function->locals[function->local_count].slot;
  function->local_count += 1;
  if (function->local_count > function->max_local_count) {
    function->max_local_count = function->local_count;
    if (!ms_resolution_table_set_function_local_count(resolver->table,
                                                      function->node_id,
                                                      function->max_local_count)) {
      return 0;
    }
  }
  return 1;
}

static int ms_resolver_declare_global(MsResolver *resolver,
                                      const MsAstNode *node,
                                      MsToken name,
                                      size_t binding_node_id) {
  size_t i;

  if (resolver == NULL) {
    return 0;
  }

  for (i = 0; i < resolver->global_count; ++i) {
    if (ms_token_equals(resolver->globals[i].name, name)) {
      return node != NULL ?
          ms_resolver_append_diagnostic(resolver,
                                        node,
                                        "MS3002",
                                        "duplicate declaration in the same scope") :
          ms_resolver_append_token_diagnostic(resolver,
                                              name,
                                              "MS3002",
                                              "duplicate declaration in the same scope");
    }
  }
  if (!ms_resolver_ensure_globals(resolver, resolver->global_count + 1)) {
    return 0;
  }

  resolver->globals[resolver->global_count].name = name;
  resolver->globals[resolver->global_count].binding_node_id = binding_node_id;
  resolver->globals[resolver->global_count].initialized = 0;
  resolver->global_count += 1;
  return 1;
}

static int ms_resolver_initialize_local(MsResolverFunctionContext *function,
                                        uint8_t slot) {
  if (function == NULL || slot >= function->local_count) {
    return 0;
  }

  function->locals[slot].initialized = 1;
  return 1;
}

static int ms_resolver_initialize_global(MsResolver *resolver, MsToken name) {
  size_t i;

  if (resolver == NULL) {
    return 0;
  }

  for (i = resolver->global_count; i > 0; --i) {
    if (ms_token_equals(resolver->globals[i - 1].name, name)) {
      resolver->globals[i - 1].initialized = 1;
      return 1;
    }
  }

  return 0;
}static MsLookupResult ms_resolver_lookup_local_in_context(
    MsResolver *resolver,
    MsResolverFunctionContext *function,
    const MsAstNode *node,
    MsToken name,
    int is_write,
    MsResolvedBinding *out_binding,
    MsResolverLocal **out_local) {
  size_t i;

  if (function == NULL) {
    return MS_LOOKUP_NOT_FOUND;
  }

  for (i = function->local_count; i > 0; --i) {
    MsResolverLocal *local = &function->locals[i - 1];

    if (!ms_token_equals(local->name, name)) {
      continue;
    }
    if (!is_write && !local->initialized) {
      ms_resolver_append_diagnostic(resolver,
                                    node,
                                    "MS3004",
                                    "cannot read local variable in its own initializer");
      return MS_LOOKUP_ERROR;
    }
    if (out_binding != NULL) {
      out_binding->node_id = node->node_id;
      out_binding->function_node_id = function->node_id;
      out_binding->kind = MS_BINDING_LOCAL;
      out_binding->slot = local->slot;
      out_binding->scope_depth = local->scope_depth;
      out_binding->lexical_depth = 0;
      out_binding->is_captured = 0;
    }
    if (out_local != NULL) {
      *out_local = local;
    }
    return MS_LOOKUP_FOUND;
  }

  return MS_LOOKUP_NOT_FOUND;
}

static MsLookupResult ms_resolver_resolve_upvalue(MsResolver *resolver,
                                                  MsResolverFunctionContext *function,
                                                  const MsAstNode *node,
                                                  MsToken name,
                                                  int is_write,
                                                  MsResolvedBinding *out_binding) {
  MsResolvedBinding binding;
  MsResolverLocal *local = NULL;
  MsLookupResult result;
  uint8_t upvalue_index = 0;

  if (resolver == NULL || function == NULL || function->enclosing == NULL) {
    return MS_LOOKUP_NOT_FOUND;
  }

  result = ms_resolver_lookup_local_in_context(resolver,
                                               function->enclosing,
                                               node,
                                               name,
                                               is_write,
                                               &binding,
                                               &local);
  if (result == MS_LOOKUP_ERROR) {
    return MS_LOOKUP_ERROR;
  }
  if (result == MS_LOOKUP_FOUND) {
    if (!ms_resolution_table_mark_function_local_captured(
            resolver->table,
            function->enclosing->node_id,
            binding.slot)) {
      return MS_LOOKUP_ERROR;
    }
    if (local != NULL && local->binding_node_id != 0 &&
        !ms_resolution_table_mark_captured(resolver->table, local->binding_node_id)) {
      return MS_LOOKUP_ERROR;
    }
    if (!ms_resolution_table_add_upvalue(resolver->table,
                                         function->node_id,
                                         1,
                                         binding.slot,
                                         1,
                                         &upvalue_index)) {
      return MS_LOOKUP_ERROR;
    }
    if (out_binding != NULL) {
      out_binding->node_id = node->node_id;
      out_binding->function_node_id = function->node_id;
      out_binding->kind = MS_BINDING_UPVALUE;
      out_binding->slot = upvalue_index;
      out_binding->scope_depth = binding.scope_depth;
      out_binding->lexical_depth = 1;
      out_binding->is_captured = 0;
    }
    return MS_LOOKUP_FOUND;
  }

  result = ms_resolver_resolve_upvalue(resolver,
                                       function->enclosing,
                                       node,
                                       name,
                                       is_write,
                                       &binding);
  if (result != MS_LOOKUP_FOUND) {
    return result;
  }

  if (!ms_resolution_table_add_upvalue(resolver->table,
                                       function->node_id,
                                       0,
                                       binding.slot,
                                       binding.lexical_depth + 1,
                                       &upvalue_index)) {
    return MS_LOOKUP_ERROR;
  }
  if (out_binding != NULL) {
    out_binding->node_id = node->node_id;
    out_binding->function_node_id = function->node_id;
    out_binding->kind = MS_BINDING_UPVALUE;
    out_binding->slot = upvalue_index;
    out_binding->scope_depth = binding.scope_depth;
    out_binding->lexical_depth = binding.lexical_depth + 1;
    out_binding->is_captured = 0;
  }
  return MS_LOOKUP_FOUND;
}

static int ms_resolver_lookup_global(MsResolver *resolver,
                                     MsResolverFunctionContext *function,
                                     const MsAstNode *node,
                                     MsToken name,
                                     int is_write) {
  size_t i;

  if (resolver == NULL || function == NULL || node == NULL) {
    return 0;
  }

  for (i = resolver->global_count; i > 0; --i) {
    MsResolverGlobal *global = &resolver->globals[i - 1];

    if (!ms_token_equals(global->name, name)) {
      continue;
    }
    if (!is_write && !global->initialized) {
      return ms_resolver_append_diagnostic(
          resolver,
          node,
          "MS3004",
          "cannot read variable in its own initializer");
    }
    break;
  }

  return ms_resolution_table_set(resolver->table,
                                 node->node_id,
                                 function->node_id,
                                 MS_BINDING_GLOBAL,
                                 0,
                                 0,
                                 0,
                                 0);
}

static int ms_resolver_resolve_variable(MsResolver *resolver,
                                        const MsAstNode *node,
                                        MsToken name,
                                        int is_write) {
  MsResolvedBinding binding;
  MsLookupResult result;

  if (resolver == NULL || node == NULL || resolver->current_function == NULL) {
    return 0;
  }

  result = ms_resolver_lookup_local_in_context(resolver,
                                               resolver->current_function,
                                               node,
                                               name,
                                               is_write,
                                               &binding,
                                               NULL);
  if (result == MS_LOOKUP_ERROR) {
    return 0;
  }
  if (result == MS_LOOKUP_FOUND) {
    return ms_resolution_table_set(resolver->table,
                                   node->node_id,
                                   binding.function_node_id,
                                   binding.kind,
                                   binding.slot,
                                   binding.scope_depth,
                                   binding.lexical_depth,
                                   binding.is_captured);
  }

  result = ms_resolver_resolve_upvalue(resolver,
                                       resolver->current_function,
                                       node,
                                       name,
                                       is_write,
                                       &binding);
  if (result == MS_LOOKUP_ERROR) {
    return 0;
  }
  if (result == MS_LOOKUP_FOUND) {
    return ms_resolution_table_set(resolver->table,
                                   node->node_id,
                                   binding.function_node_id,
                                   binding.kind,
                                   binding.slot,
                                   binding.scope_depth,
                                   binding.lexical_depth,
                                   binding.is_captured);
  }

  return ms_resolver_lookup_global(resolver,
                                   resolver->current_function,
                                   node,
                                   name,
                                   is_write);
}

static int ms_resolver_resolve_expression(MsResolver *resolver,
                                          const MsAstNode *node);
static int ms_resolver_resolve_statement(MsResolver *resolver,
                                         const MsAstNode *node);

static int ms_resolver_resolve_unsupported(MsResolver *resolver,
                                           const MsAstNode *node) {
  return ms_resolver_append_diagnostic(resolver,
                                       node,
                                       "MS3005",
                                       "unsupported feature in resolver");
}

static int ms_resolver_bind_declaration(MsResolver *resolver,
                                        const MsAstNode *node,
                                        MsToken name,
                                        uint8_t *out_slot,
                                        MsBindingKind *out_kind) {
  MsResolverFunctionContext *function = resolver != NULL ?
      resolver->current_function :
      NULL;

  if (resolver == NULL || node == NULL || function == NULL || out_kind == NULL) {
    return 0;
  }

  if (function->scope_depth > 0) {
    uint8_t slot = 0;

    if (!ms_resolver_declare_local(resolver,
                                   function,
                                   node,
                                   name,
                                   node->node_id,
                                   &slot) ||
        !ms_resolution_table_set(resolver->table,
                                 node->node_id,
                                 function->node_id,
                                 MS_BINDING_LOCAL,
                                 slot,
                                 function->scope_depth,
                                 0,
                                 0)) {
      return 0;
    }
    if (out_slot != NULL) {
      *out_slot = slot;
    }
    *out_kind = MS_BINDING_LOCAL;
    return 1;
  }

  if (!ms_resolver_declare_global(resolver, node, name, node->node_id) ||
      !ms_resolution_table_set(resolver->table,
                               node->node_id,
                               function->node_id,
                               MS_BINDING_GLOBAL,
                               0,
                               0,
                               0,
                               0)) {
    return 0;
  }

  if (out_slot != NULL) {
    *out_slot = 0;
  }
  *out_kind = MS_BINDING_GLOBAL;
  return 1;
}

static int ms_resolver_initialize_declaration(MsResolver *resolver,
                                              MsToken name,
                                              MsBindingKind kind,
                                              uint8_t slot) {
  if (resolver == NULL || resolver->current_function == NULL) {
    return 0;
  }

  if (kind == MS_BINDING_LOCAL) {
    return ms_resolver_initialize_local(resolver->current_function, slot);
  }

  return ms_resolver_initialize_global(resolver, name);
}static int ms_resolver_resolve_function_body(MsResolver *resolver,
                                             size_t node_id,
                                             const MsTokenArray *parameters,
                                             const MsAstNode *body,
                                             unsigned flags) {
  MsResolverFunctionContext function;
  size_t i;
  int result = 0;

  if (resolver == NULL || body == NULL || parameters == NULL) {
    return 0;
  }

  memset(&function, 0, sizeof(function));
  function.node_id = node_id;
  function.is_function = 1;
  function.flags = flags;
  function.enclosing = resolver->current_function;

  if (!ms_resolution_table_set_function(resolver->table,
                                        node_id,
                                        function.enclosing != NULL ? function.enclosing->node_id : 0,
                                        flags)) {
    return 0;
  }

  resolver->current_function = &function;
  if (!ms_resolver_begin_scope(&function)) {
    goto cleanup;
  }

  for (i = 0; i < parameters->count; ++i) {
    uint8_t slot = 0;

    if (!ms_resolver_declare_local(resolver,
                                   &function,
                                   NULL,
                                   parameters->items[i],
                                   0,
                                   &slot) ||
        !ms_resolver_initialize_local(&function, slot)) {
      goto cleanup;
    }
  }

  if (!ms_resolver_resolve_statement(resolver, body)) {
    goto cleanup;
  }

  result = 1;

cleanup:
  resolver->current_function = function.enclosing;
  free(function.scope_markers);
  free(function.locals);
  return result;
}

static int ms_resolver_resolve_expression(MsResolver *resolver,
                                          const MsAstNode *node) {
  size_t i;

  if (resolver == NULL || node == NULL) {
    return 0;
  }

  switch (node->kind) {
    case MS_AST_LITERAL:
      return 1;
    case MS_AST_VARIABLE:
      return ms_resolver_resolve_variable(resolver,
                                          node,
                                          node->as.variable.name,
                                          0);
    case MS_AST_UNARY:
      return ms_resolver_resolve_expression(resolver, node->as.unary.operand);
    case MS_AST_BINARY:
      return ms_resolver_resolve_expression(resolver, node->as.binary.left) &&
             ms_resolver_resolve_expression(resolver, node->as.binary.right);
    case MS_AST_LOGICAL:
      return ms_resolver_resolve_expression(resolver, node->as.logical.left) &&
             ms_resolver_resolve_expression(resolver, node->as.logical.right);
    case MS_AST_ASSIGN:
      if (node->as.assign.target == NULL ||
          node->as.assign.target->kind != MS_AST_VARIABLE) {
        return ms_resolver_resolve_unsupported(resolver, node->as.assign.target);
      }
      return ms_resolver_resolve_expression(resolver, node->as.assign.value) &&
             ms_resolver_resolve_variable(resolver,
                                          node->as.assign.target,
                                          node->as.assign.target->as.variable.name,
                                          1);
    case MS_AST_CALL:
      if (!ms_resolver_resolve_expression(resolver, node->as.call.callee)) {
        return 0;
      }
      for (i = 0; i < node->as.call.arguments.count; ++i) {
        if (!ms_resolver_resolve_expression(resolver,
                                            node->as.call.arguments.items[i])) {
          return 0;
        }
      }
      return 1;
    case MS_AST_PROPERTY:
      return ms_resolver_resolve_expression(resolver, node->as.property.object);
    case MS_AST_INDEX:
      return ms_resolver_resolve_expression(resolver, node->as.index.object) &&
             ms_resolver_resolve_expression(resolver, node->as.index.index);
    case MS_AST_LIST:
    case MS_AST_TUPLE:
      for (i = 0; i < node->as.list.elements.count; ++i) {
        if (!ms_resolver_resolve_expression(resolver,
                                            node->as.list.elements.items[i])) {
          return 0;
        }
      }
      return 1;
    case MS_AST_MAP:
      for (i = 0; i < node->as.map.entries.count; ++i) {
        if (!ms_resolver_resolve_expression(resolver,
                                            node->as.map.entries.items[i].key) ||
            !ms_resolver_resolve_expression(resolver,
                                            node->as.map.entries.items[i].value)) {
          return 0;
        }
      }
      return 1;
    case MS_AST_FUNCTION:
      return ms_resolver_resolve_function_body(resolver,
                                               node->node_id,
                                               &node->as.function.parameters,
                                               node->as.function.body,
                                               MS_FUNCTION_FLAG_NONE);
    case MS_AST_SELF:
    case MS_AST_SUPER:
      return ms_resolver_resolve_unsupported(resolver, node);
    default:
      return ms_resolver_resolve_unsupported(resolver, node);
  }
}static int ms_resolver_resolve_statement(MsResolver *resolver,
                                         const MsAstNode *node) {
  size_t i;

  if (resolver == NULL || node == NULL || resolver->current_function == NULL) {
    return 0;
  }

  switch (node->kind) {
    case MS_AST_PROGRAM:
      for (i = 0; i < node->as.program.declarations.count; ++i) {
        if (!ms_resolver_resolve_statement(resolver,
                                           node->as.program.declarations.items[i])) {
          return 0;
        }
      }
      return 1;
    case MS_AST_BLOCK:
      if (!ms_resolver_begin_scope(resolver->current_function)) {
        return 0;
      }
      for (i = 0; i < node->as.block.statements.count; ++i) {
        if (!ms_resolver_resolve_statement(resolver,
                                           node->as.block.statements.items[i])) {
          return 0;
        }
      }
      return ms_resolver_end_scope(resolver->current_function);
    case MS_AST_VAR_DECL: {
      MsBindingKind kind;
      uint8_t slot = 0;

      if (!ms_resolver_bind_declaration(resolver,
                                        node,
                                        node->as.var_decl.name,
                                        &slot,
                                        &kind)) {
        return 0;
      }
      if (node->as.var_decl.initializer != NULL &&
          !ms_resolver_resolve_expression(resolver,
                                          node->as.var_decl.initializer)) {
        return 0;
      }
      return ms_resolver_initialize_declaration(resolver,
                                                node->as.var_decl.name,
                                                kind,
                                                slot);
    }
    case MS_AST_FUNCTION_DECL: {
      MsBindingKind kind;
      uint8_t slot = 0;

      if (!ms_resolver_bind_declaration(resolver,
                                        node,
                                        node->as.function_decl.name,
                                        &slot,
                                        &kind) ||
          !ms_resolver_initialize_declaration(resolver,
                                              node->as.function_decl.name,
                                              kind,
                                              slot)) {
        return 0;
      }
      return ms_resolver_resolve_function_body(resolver,
                                               node->node_id,
                                               &node->as.function_decl.parameters,
                                               node->as.function_decl.body,
                                               MS_FUNCTION_FLAG_NONE);
    }
    case MS_AST_EXPR_STMT:
      return ms_resolver_resolve_expression(resolver, node->as.expr_stmt.expression);
    case MS_AST_PRINT_STMT:
      return ms_resolver_resolve_expression(resolver, node->as.print_stmt.expression);
    case MS_AST_IF_STMT:
      if (!ms_resolver_resolve_expression(resolver, node->as.if_stmt.condition) ||
          !ms_resolver_resolve_statement(resolver, node->as.if_stmt.then_branch)) {
        return 0;
      }
      if (node->as.if_stmt.else_branch != NULL) {
        return ms_resolver_resolve_statement(resolver,
                                             node->as.if_stmt.else_branch);
      }
      return 1;
    case MS_AST_WHILE_STMT:
      if (!ms_resolver_resolve_expression(resolver, node->as.while_stmt.condition)) {
        return 0;
      }
      resolver->current_function->loop_depth += 1;
      if (!ms_resolver_resolve_statement(resolver, node->as.while_stmt.body)) {
        resolver->current_function->loop_depth -= 1;
        return 0;
      }
      resolver->current_function->loop_depth -= 1;
      return 1;
    case MS_AST_FOR_STMT:
      if (!ms_resolver_begin_scope(resolver->current_function)) {
        return 0;
      }
      if (node->as.for_stmt.initializer != NULL &&
          !ms_resolver_resolve_statement(resolver, node->as.for_stmt.initializer)) {
        return 0;
      }
      if (node->as.for_stmt.condition != NULL &&
          !ms_resolver_resolve_expression(resolver, node->as.for_stmt.condition)) {
        return 0;
      }
      resolver->current_function->loop_depth += 1;
      if (node->as.for_stmt.increment != NULL &&
          !ms_resolver_resolve_expression(resolver, node->as.for_stmt.increment)) {
        resolver->current_function->loop_depth -= 1;
        return 0;
      }
      if (!ms_resolver_resolve_statement(resolver, node->as.for_stmt.body)) {
        resolver->current_function->loop_depth -= 1;
        return 0;
      }
      resolver->current_function->loop_depth -= 1;
      return ms_resolver_end_scope(resolver->current_function);
    case MS_AST_BREAK_STMT:
    case MS_AST_CONTINUE_STMT:
      if (resolver->current_function->loop_depth == 0) {
        return ms_resolver_append_diagnostic(resolver,
                                             node,
                                             "MS3003",
                                             node->kind == MS_AST_BREAK_STMT ?
                                                 "break is only allowed inside loops" :
                                                 "continue is only allowed inside loops");
      }
      return 1;
    case MS_AST_RETURN_STMT:
      if (!resolver->current_function->is_function) {
        return ms_resolver_append_diagnostic(resolver,
                                             node,
                                             "MS3001",
                                             "top-level return is not allowed");
      }
      if (node->as.return_stmt.value != NULL) {
        return ms_resolver_resolve_expression(resolver,
                                              node->as.return_stmt.value);
      }
      return 1;
    case MS_AST_CLASS_DECL:
    case MS_AST_IMPORT_STMT:
    case MS_AST_FROM_IMPORT_STMT:
      return ms_resolver_resolve_unsupported(resolver, node);
    default:
      return ms_resolver_resolve_unsupported(resolver, node);
  }
}

int ms_resolve_program(const char *file,
                       const MsAstNode *program,
                       MsResolutionTable *table,
                       MsDiagnosticList *diagnostics) {
  MsResolver resolver;
  MsResolverFunctionContext module_context;

  if (program == NULL || table == NULL || diagnostics == NULL) {
    return 0;
  }

  memset(&resolver, 0, sizeof(resolver));
  memset(&module_context, 0, sizeof(module_context));
  resolver.file = file != NULL ? file : "<unknown>";
  resolver.table = table;
  resolver.diagnostics = diagnostics;
  module_context.node_id = 0;
  module_context.is_function = 0;
  resolver.current_function = &module_context;

  ms_resolution_table_clear(table);
  if (!ms_resolution_table_set_function(table, 0, 0, MS_FUNCTION_FLAG_NONE) ||
      !ms_resolver_resolve_statement(&resolver, program)) {
    free(resolver.globals);
    free(module_context.scope_markers);
    free(module_context.locals);
    return 0;
  }

  free(resolver.globals);
  free(module_context.scope_markers);
  free(module_context.locals);
  return !resolver.had_error;
}