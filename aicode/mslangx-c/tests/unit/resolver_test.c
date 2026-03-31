#include <string.h>

#include "ms/arena.h"
#include "ms/ast.h"
#include "ms/diag.h"
#include "ms/frontend/resolution_table.h"
#include "ms/frontend/resolver.h"
#include "ms/parser.h"

#include "test_assert.h"

static MsAstNode *parse_program(const char *source,
                                MsArena *arena,
                                MsDiagnosticList *diagnostics) {
  ms_arena_init(arena, 4096);
  ms_diag_list_init(diagnostics);
  return ms_parse_source("<unit>", source, arena, diagnostics);
}

static int test_rejects_top_level_return(void) {
  MsArena arena;
  MsDiagnosticList diagnostics;
  MsResolutionTable table;
  MsAstNode *root;
  const MsDiagnostic *diagnostic;

  root = parse_program("return 1\n", &arena, &diagnostics);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  ms_resolution_table_init(&table);
  TEST_ASSERT(!ms_resolve_program("<unit>", root, &table, &diagnostics));
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 1);

  diagnostic = ms_diag_list_at(&diagnostics, 0);
  TEST_ASSERT(diagnostic != NULL);
  TEST_ASSERT(strcmp(diagnostic->phase, "resolve") == 0);
  TEST_ASSERT(strcmp(diagnostic->code, "MS3001") == 0);
  TEST_ASSERT(strcmp(diagnostic->message, "top-level return is not allowed") == 0);

  ms_resolution_table_destroy(&table);
  ms_diag_list_destroy(&diagnostics);
  ms_arena_destroy(&arena);
  return 0;
}

static int test_resolves_function_declaration_and_parameter_reads(void) {
  static const char kSource[] =
      "fn identity(value) {\n"
      "  return value\n"
      "}\n";
  MsArena arena;
  MsDiagnosticList diagnostics;
  MsResolutionTable table;
  MsAstNode *root;
  MsAstNode *function_decl;
  MsAstNode *return_stmt;
  MsAstNode *variable_expr;
  MsResolvedBinding binding;

  root = parse_program(kSource, &arena, &diagnostics);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  function_decl = root->as.program.declarations.items[0];
  TEST_ASSERT(function_decl->kind == MS_AST_FUNCTION_DECL);
  TEST_ASSERT(function_decl->as.function_decl.body != NULL);
  TEST_ASSERT(function_decl->as.function_decl.body->kind == MS_AST_BLOCK);
  return_stmt = function_decl->as.function_decl.body->as.block.statements.items[0];
  TEST_ASSERT(return_stmt->kind == MS_AST_RETURN_STMT);
  variable_expr = return_stmt->as.return_stmt.value;
  TEST_ASSERT(variable_expr != NULL);
  TEST_ASSERT(variable_expr->kind == MS_AST_VARIABLE);

  ms_resolution_table_init(&table);
  TEST_ASSERT(ms_resolve_program("<unit>", root, &table, &diagnostics));
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, function_decl->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_GLOBAL);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.scope_depth == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, variable_expr->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.scope_depth == 1);

  ms_resolution_table_destroy(&table);
  ms_diag_list_destroy(&diagnostics);
  ms_arena_destroy(&arena);
  return 0;
}

static int test_resolves_function_expression_parameter_reads(void) {
  static const char kSource[] =
      "var identity = fn(value) {\n"
      "  return value\n"
      "}\n";
  MsArena arena;
  MsDiagnosticList diagnostics;
  MsResolutionTable table;
  MsAstNode *root;
  MsAstNode *var_decl;
  MsAstNode *function_expr;
  MsAstNode *return_stmt;
  MsAstNode *variable_expr;
  MsResolvedBinding binding;

  root = parse_program(kSource, &arena, &diagnostics);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  var_decl = root->as.program.declarations.items[0];
  TEST_ASSERT(var_decl->kind == MS_AST_VAR_DECL);
  function_expr = var_decl->as.var_decl.initializer;
  TEST_ASSERT(function_expr != NULL);
  TEST_ASSERT(function_expr->kind == MS_AST_FUNCTION);
  return_stmt = function_expr->as.function.body->as.block.statements.items[0];
  TEST_ASSERT(return_stmt->kind == MS_AST_RETURN_STMT);
  variable_expr = return_stmt->as.return_stmt.value;
  TEST_ASSERT(variable_expr != NULL);
  TEST_ASSERT(variable_expr->kind == MS_AST_VARIABLE);

  ms_resolution_table_init(&table);
  TEST_ASSERT(ms_resolve_program("<unit>", root, &table, &diagnostics));
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, var_decl->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_GLOBAL);
  TEST_ASSERT(binding.scope_depth == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, variable_expr->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.scope_depth == 1);

  ms_resolution_table_destroy(&table);
  ms_diag_list_destroy(&diagnostics);
  ms_arena_destroy(&arena);
  return 0;
}

static int test_resolves_nested_function_upvalues(void) {
  static const char kSource[] =
      "fn outer() {\n"
      "  var local = 1\n"
      "  fn inner() {\n"
      "    print local\n"
      "  }\n"
      "  return inner\n"
      "}\n";
  MsArena arena;
  MsDiagnosticList diagnostics;
  MsResolutionTable table;
  MsAstNode *root;
  MsAstNode *outer_decl;
  MsAstNode *outer_body;
  MsAstNode *local_decl;
  MsAstNode *inner_decl;
  MsAstNode *print_stmt;
  MsAstNode *captured_expr;
  MsResolvedBinding binding;
  MsFunctionResolution function_info;

  root = parse_program(kSource, &arena, &diagnostics);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  outer_decl = root->as.program.declarations.items[0];
  TEST_ASSERT(outer_decl->kind == MS_AST_FUNCTION_DECL);
  outer_body = outer_decl->as.function_decl.body;
  TEST_ASSERT(outer_body != NULL);
  TEST_ASSERT(outer_body->kind == MS_AST_BLOCK);
  local_decl = outer_body->as.block.statements.items[0];
  inner_decl = outer_body->as.block.statements.items[1];
  TEST_ASSERT(local_decl->kind == MS_AST_VAR_DECL);
  TEST_ASSERT(inner_decl->kind == MS_AST_FUNCTION_DECL);
  print_stmt = inner_decl->as.function_decl.body->as.block.statements.items[0];
  TEST_ASSERT(print_stmt->kind == MS_AST_PRINT_STMT);
  captured_expr = print_stmt->as.print_stmt.expression;
  TEST_ASSERT(captured_expr != NULL);
  TEST_ASSERT(captured_expr->kind == MS_AST_VARIABLE);

  ms_resolution_table_init(&table);
  TEST_ASSERT(ms_resolve_program("<unit>", root, &table, &diagnostics));
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, local_decl->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.scope_depth == 2);
  TEST_ASSERT(binding.is_captured == 1);

  TEST_ASSERT(ms_resolution_table_get(&table, captured_expr->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_UPVALUE);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.scope_depth == 2);
  TEST_ASSERT(binding.lexical_depth == 1);
  TEST_ASSERT(binding.function_node_id == inner_decl->node_id);

  TEST_ASSERT(ms_resolution_table_get_function(&table,
                                               outer_decl->node_id,
                                               &function_info));
  TEST_ASSERT(function_info.local_count == 2);
  TEST_ASSERT(function_info.upvalue_count == 0);

  TEST_ASSERT(ms_resolution_table_get_function(&table,
                                               inner_decl->node_id,
                                               &function_info));
  TEST_ASSERT(function_info.local_count == 0);
  TEST_ASSERT(function_info.upvalue_count == 1);
  TEST_ASSERT(function_info.upvalues[0].index == 0);
  TEST_ASSERT(function_info.upvalues[0].is_local == 1);
  TEST_ASSERT(function_info.upvalues[0].slot == 0);
  TEST_ASSERT(function_info.upvalues[0].lexical_depth == 1);

  ms_resolution_table_destroy(&table);
  ms_diag_list_destroy(&diagnostics);
  ms_arena_destroy(&arena);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_rejects_top_level_return() == 0);
  TEST_ASSERT(test_resolves_function_declaration_and_parameter_reads() == 0);
  TEST_ASSERT(test_resolves_function_expression_parameter_reads() == 0);
  TEST_ASSERT(test_resolves_nested_function_upvalues() == 0);
  return 0;
}