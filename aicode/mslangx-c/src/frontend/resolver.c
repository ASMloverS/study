#include "ms/frontend/resolver.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct MsResolverLocal {
  MsToken name;
  int scope_depth;
  int initialized;
  uint8_t slot;
} MsResolverLocal;

typedef struct MsResolverGlobal {
  MsToken name;
  int initialized;
} MsResolverGlobal;

typedef struct MsResolver {
  const char *file;
  MsResolutionTable *table;
  MsDiagnosticList *diagnostics;
  MsResolverLocal *locals;
  size_t local_count;
  size_t local_capacity;
  size_t *scope_markers;
  size_t scope_count;
  size_t scope_capacity;
  MsResolverGlobal *globals;
  size_t global_count;
  size_t global_capacity;
  int scope_depth;
  int loop_depth;
  int had_error;
} MsResolver;

static int ms_token_equals(MsToken left, MsToken right) {
  return left.length == right.length &&
         memcmp(left.start, right.start, left.length) == 0;
}

static int ms_resolver_append_diagnostic(MsResolver *resolver,
                                         const MsAstNode *node,
                                         const char *code,
                                         const char *message) {
  MsDiagnostic diagnostic;

  if (resolver == NULL || node == NULL || resolver->diagnostics == NULL) {
    return 0;
  }

  diagnostic.phase = "resolve";
  diagnostic.code = code;
  diagnostic.message = message;
  diagnostic.span.file = resolver->file;
  diagnostic.span.line = node->line;
  diagnostic.span.column = node->column;
  diagnostic.span.length = node->end_column - node->column + 1;
  resolver->had_error = 1;
  return ms_diag_list_append(resolver->diagnostics, &diagnostic);
}

static int ms_resolver_ensure_locals(MsResolver *resolver, size_t min_capacity) {
  MsResolverLocal *locals;
  size_t new_capacity;

  if (resolver == NULL) {
    return 0;
  }
  if (min_capacity <= resolver->local_capacity) {
    return 1;
  }

  new_capacity = resolver->local_capacity == 0 ? 16 : resolver->local_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  locals = (MsResolverLocal *) realloc(resolver->locals,
                                       new_capacity * sizeof(*locals));
  if (locals == NULL) {
    return 0;
  }

  resolver->locals = locals;
  resolver->local_capacity = new_capacity;
  return 1;
}

static int ms_resolver_ensure_scope_markers(MsResolver *resolver,
                                            size_t min_capacity) {
  size_t *markers;
  size_t new_capacity;

  if (resolver == NULL) {
    return 0;
  }
  if (min_capacity <= resolver->scope_capacity) {
    return 1;
  }

  new_capacity = resolver->scope_capacity == 0 ? 8 : resolver->scope_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  markers = (size_t *) realloc(resolver->scope_markers,
                               new_capacity * sizeof(*markers));
  if (markers == NULL) {
    return 0;
  }

  resolver->scope_markers = markers;
  resolver->scope_capacity = new_capacity;
  return 1;
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

static int ms_resolver_begin_scope(MsResolver *resolver) {
  if (resolver == NULL ||
      !ms_resolver_ensure_scope_markers(resolver, resolver->scope_count + 1)) {
    return 0;
  }

  resolver->scope_markers[resolver->scope_count] = resolver->local_count;
  resolver->scope_count += 1;
  resolver->scope_depth += 1;
  return 1;
}

static int ms_resolver_end_scope(MsResolver *resolver) {
  if (resolver == NULL || resolver->scope_count == 0) {
    return 0;
  }

  resolver->scope_count -= 1;
  resolver->local_count = resolver->scope_markers[resolver->scope_count];
  resolver->scope_depth -= 1;
  return 1;
}

static int ms_resolver_local_scope_start(const MsResolver *resolver,
                                         size_t *out_start) {
  if (resolver == NULL || out_start == NULL) {
    return 0;
  }

  if (resolver->scope_count == 0) {
    *out_start = 0;
  } else {
    *out_start = resolver->scope_markers[resolver->scope_count - 1];
  }
  return 1;
}

static int ms_resolver_declare_local(MsResolver *resolver,
                                     const MsAstNode *node,
                                     MsToken name,
                                     uint8_t *out_slot) {
  size_t scope_start = 0;
  size_t i;

  if (resolver == NULL || out_slot == NULL) {
    return 0;
  }
  if (resolver->local_count > UCHAR_MAX) {
    return ms_resolver_append_diagnostic(resolver,
                                         node,
                                         "MS3006",
                                         "too many local variables in basic lowering");
  }
  if (!ms_resolver_local_scope_start(resolver, &scope_start)) {
    return 0;
  }

  for (i = resolver->local_count; i > scope_start; --i) {
    if (ms_token_equals(resolver->locals[i - 1].name, name)) {
      return ms_resolver_append_diagnostic(resolver,
                                           node,
                                           "MS3002",
                                           "duplicate declaration in the same scope");
    }
  }
  if (!ms_resolver_ensure_locals(resolver, resolver->local_count + 1)) {
    return 0;
  }

  resolver->locals[resolver->local_count].name = name;
  resolver->locals[resolver->local_count].scope_depth = resolver->scope_depth;
  resolver->locals[resolver->local_count].initialized = 0;
  resolver->locals[resolver->local_count].slot = (uint8_t) resolver->local_count;
  *out_slot = resolver->locals[resolver->local_count].slot;
  resolver->local_count += 1;
  return 1;
}

static int ms_resolver_declare_global(MsResolver *resolver,
                                      const MsAstNode *node,
                                      MsToken name) {
  size_t i;

  if (resolver == NULL) {
    return 0;
  }

  for (i = 0; i < resolver->global_count; ++i) {
    if (ms_token_equals(resolver->globals[i].name, name)) {
      return ms_resolver_append_diagnostic(resolver,
                                           node,
                                           "MS3002",
                                           "duplicate declaration in the same scope");
    }
  }
  if (!ms_resolver_ensure_globals(resolver, resolver->global_count + 1)) {
    return 0;
  }

  resolver->globals[resolver->global_count].name = name;
  resolver->globals[resolver->global_count].initialized = 0;
  resolver->global_count += 1;
  return 1;
}

static int ms_resolver_initialize_local(MsResolver *resolver, uint8_t slot) {
  if (resolver == NULL || slot >= resolver->local_count) {
    return 0;
  }

  resolver->locals[slot].initialized = 1;
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
}

static int ms_resolver_lookup_local(MsResolver *resolver,
                                    const MsAstNode *node,
                                    MsToken name,
                                    int is_write) {
  size_t i;

  for (i = resolver->local_count; i > 0; --i) {
    MsResolverLocal *local = &resolver->locals[i - 1];

    if (!ms_token_equals(local->name, name)) {
      continue;
    }
    if (!is_write && !local->initialized) {
      return ms_resolver_append_diagnostic(
          resolver,
          node,
          "MS3004",
          "cannot read local variable in its own initializer");
    }

    return ms_resolution_table_set(resolver->table,
                                   node->node_id,
                                   MS_BINDING_LOCAL,
                                   local->slot,
                                   local->scope_depth);
  }

  return 0;
}

static int ms_resolver_lookup_global(MsResolver *resolver,
                                     const MsAstNode *node,
                                     MsToken name,
                                     int is_write) {
  size_t i;

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
                                 MS_BINDING_GLOBAL,
                                 0,
                                 0);
}

static int ms_resolver_resolve_variable(MsResolver *resolver,
                                        const MsAstNode *node,
                                        MsToken name,
                                        int is_write) {
  if (resolver == NULL || node == NULL) {
    return 0;
  }

  if (ms_resolver_lookup_local(resolver, node, name, is_write)) {
    return 1;
  }

  return ms_resolver_lookup_global(resolver, node, name, is_write);
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
                                       "unsupported feature in basic lowering");
}

static int ms_resolver_resolve_expression(MsResolver *resolver,
                                          const MsAstNode *node) {
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
      return ms_resolver_resolve_variable(resolver,
                                          node->as.assign.target,
                                          node->as.assign.target->as.variable.name,
                                          1) &&
             ms_resolver_resolve_expression(resolver, node->as.assign.value);
    case MS_AST_FUNCTION:
    case MS_AST_CALL:
    case MS_AST_PROPERTY:
    case MS_AST_INDEX:
    case MS_AST_LIST:
    case MS_AST_TUPLE:
    case MS_AST_MAP:
    case MS_AST_SELF:
    case MS_AST_SUPER:
      return ms_resolver_resolve_unsupported(resolver, node);
    default:
      return ms_resolver_resolve_unsupported(resolver, node);
  }
}

static int ms_resolver_resolve_statement(MsResolver *resolver,
                                         const MsAstNode *node) {
  size_t i;

  if (resolver == NULL || node == NULL) {
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
      if (!ms_resolver_begin_scope(resolver)) {
        return 0;
      }
      for (i = 0; i < node->as.block.statements.count; ++i) {
        if (!ms_resolver_resolve_statement(resolver,
                                           node->as.block.statements.items[i])) {
          return 0;
        }
      }
      return ms_resolver_end_scope(resolver);
    case MS_AST_VAR_DECL:
      if (resolver->scope_depth > 0) {
        uint8_t slot = 0;

        if (!ms_resolver_declare_local(resolver,
                                       node,
                                       node->as.var_decl.name,
                                       &slot) ||
            !ms_resolution_table_set(resolver->table,
                                     node->node_id,
                                     MS_BINDING_LOCAL,
                                     slot,
                                     resolver->scope_depth)) {
          return 0;
        }
        if (node->as.var_decl.initializer != NULL &&
            !ms_resolver_resolve_expression(resolver,
                                            node->as.var_decl.initializer)) {
          return 0;
        }
        return ms_resolver_initialize_local(resolver, slot);
      }

      if (!ms_resolver_declare_global(resolver, node, node->as.var_decl.name) ||
          !ms_resolution_table_set(resolver->table,
                                   node->node_id,
                                   MS_BINDING_GLOBAL,
                                   0,
                                   0)) {
        return 0;
      }
      if (node->as.var_decl.initializer != NULL &&
          !ms_resolver_resolve_expression(resolver,
                                          node->as.var_decl.initializer)) {
        return 0;
      }
      return ms_resolver_initialize_global(resolver, node->as.var_decl.name);
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
      resolver->loop_depth += 1;
      if (!ms_resolver_resolve_statement(resolver, node->as.while_stmt.body)) {
        resolver->loop_depth -= 1;
        return 0;
      }
      resolver->loop_depth -= 1;
      return 1;
    case MS_AST_FOR_STMT:
      if (!ms_resolver_begin_scope(resolver)) {
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
      resolver->loop_depth += 1;
      if (node->as.for_stmt.increment != NULL &&
          !ms_resolver_resolve_expression(resolver, node->as.for_stmt.increment)) {
        resolver->loop_depth -= 1;
        return 0;
      }
      if (!ms_resolver_resolve_statement(resolver, node->as.for_stmt.body)) {
        resolver->loop_depth -= 1;
        return 0;
      }
      resolver->loop_depth -= 1;
      return ms_resolver_end_scope(resolver);
    case MS_AST_BREAK_STMT:
    case MS_AST_CONTINUE_STMT:
      if (resolver->loop_depth == 0) {
        return ms_resolver_append_diagnostic(resolver,
                                             node,
                                             "MS3003",
                                             node->kind == MS_AST_BREAK_STMT ?
                                                 "break is only allowed inside loops" :
                                                 "continue is only allowed inside loops");
      }
      return 1;
    case MS_AST_RETURN_STMT:
      return ms_resolver_append_diagnostic(resolver,
                                           node,
                                           "MS3001",
                                           "top-level return is not allowed");
    case MS_AST_FUNCTION_DECL:
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

  if (program == NULL || table == NULL || diagnostics == NULL) {
    return 0;
  }

  resolver.file = file != NULL ? file : "<unknown>";
  resolver.table = table;
  resolver.diagnostics = diagnostics;
  resolver.locals = NULL;
  resolver.local_count = 0;
  resolver.local_capacity = 0;
  resolver.scope_markers = NULL;
  resolver.scope_count = 0;
  resolver.scope_capacity = 0;
  resolver.globals = NULL;
  resolver.global_count = 0;
  resolver.global_capacity = 0;
  resolver.scope_depth = 0;
  resolver.loop_depth = 0;
  resolver.had_error = 0;

  ms_resolution_table_clear(table);
  if (!ms_resolver_resolve_statement(&resolver, program)) {
    free(resolver.globals);
    free(resolver.scope_markers);
    free(resolver.locals);
    return 0;
  }

  free(resolver.globals);
  free(resolver.scope_markers);
  free(resolver.locals);
  return !resolver.had_error;
}