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
  TEST_ASSERT(diagnostic->span.line == 1);
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

static int test_resolves_method_self_binding_and_nested_capture(void) {
  static const char kSource[] =
      "class Example {\n"
      "  run() {\n"
      "    print self\n"
      "    fn inner() {\n"
      "      print self\n"
      "    }\n"
      "  }\n"
      "}\n";
  MsArena arena;
  MsDiagnosticList diagnostics;
  MsResolutionTable table;
  MsAstNode *root;
  MsAstNode *class_decl;
  MsAstNode *method_decl;
  MsAstNode *method_body;
  MsAstNode *direct_print_stmt;
  MsAstNode *direct_self_expr;
  MsAstNode *inner_decl;
  MsAstNode *inner_print_stmt;
  MsAstNode *nested_self_expr;
  MsResolvedBinding binding;
  MsFunctionResolution method_info;
  MsFunctionResolution inner_info;
  int is_captured = 0;

  root = parse_program(kSource, &arena, &diagnostics);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  class_decl = root->as.program.declarations.items[0];
  TEST_ASSERT(class_decl->kind == MS_AST_CLASS_DECL);
  TEST_ASSERT(class_decl->as.class_decl.methods.count == 1);
  method_decl = class_decl->as.class_decl.methods.items[0];
  TEST_ASSERT(method_decl->kind == MS_AST_FUNCTION_DECL);
  method_body = method_decl->as.function_decl.body;
  TEST_ASSERT(method_body != NULL);
  TEST_ASSERT(method_body->kind == MS_AST_BLOCK);
  TEST_ASSERT(method_body->as.block.statements.count == 2);
  direct_print_stmt = method_body->as.block.statements.items[0];
  TEST_ASSERT(direct_print_stmt->kind == MS_AST_PRINT_STMT);
  direct_self_expr = direct_print_stmt->as.print_stmt.expression;
  TEST_ASSERT(direct_self_expr != NULL);
  TEST_ASSERT(direct_self_expr->kind == MS_AST_SELF);
  inner_decl = method_body->as.block.statements.items[1];
  TEST_ASSERT(inner_decl->kind == MS_AST_FUNCTION_DECL);
  inner_print_stmt = inner_decl->as.function_decl.body->as.block.statements.items[0];
  TEST_ASSERT(inner_print_stmt->kind == MS_AST_PRINT_STMT);
  nested_self_expr = inner_print_stmt->as.print_stmt.expression;
  TEST_ASSERT(nested_self_expr != NULL);
  TEST_ASSERT(nested_self_expr->kind == MS_AST_SELF);

  ms_resolution_table_init(&table);
  TEST_ASSERT(ms_resolve_program("<unit>", root, &table, &diagnostics));
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, class_decl->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_GLOBAL);

  TEST_ASSERT(ms_resolution_table_get(&table, direct_self_expr->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.scope_depth == 1);

  TEST_ASSERT(ms_resolution_table_get(&table, nested_self_expr->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_UPVALUE);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.lexical_depth == 1);
  TEST_ASSERT(binding.function_node_id == inner_decl->node_id);

  TEST_ASSERT(ms_resolution_table_get_function(&table,
                                               method_decl->node_id,
                                               &method_info));
  TEST_ASSERT((method_info.flags & MS_FUNCTION_FLAG_METHOD) != 0);
  TEST_ASSERT((method_info.flags & MS_FUNCTION_FLAG_HAS_SELF) != 0);
  TEST_ASSERT((method_info.flags & MS_FUNCTION_FLAG_HAS_SUPER) == 0);
  TEST_ASSERT(ms_resolution_table_function_local_is_captured(&table,
                                                             method_decl->node_id,
                                                             0,
                                                             &is_captured));
  TEST_ASSERT(is_captured);

  TEST_ASSERT(ms_resolution_table_get_function(&table,
                                               inner_decl->node_id,
                                               &inner_info));
  TEST_ASSERT(inner_info.upvalue_count == 1);
  TEST_ASSERT(inner_info.upvalues[0].is_local == 1);
  TEST_ASSERT(inner_info.upvalues[0].slot == 0);
  TEST_ASSERT(inner_info.upvalues[0].lexical_depth == 1);

  ms_resolution_table_destroy(&table);
  ms_diag_list_destroy(&diagnostics);
  ms_arena_destroy(&arena);
  return 0;
}

static int test_resolves_subclass_super_binding_and_nested_capture(void) {
  static const char kSource[] =
      "class Base {\n"
      "  run() {\n"
      "    return 1\n"
      "  }\n"
      "}\n"
      "class Derived < Base {\n"
      "  run() {\n"
      "    print super.run\n"
      "    fn inner() {\n"
      "      print super.run\n"
      "    }\n"
      "  }\n"
      "}\n";
  MsArena arena;
  MsDiagnosticList diagnostics;
  MsResolutionTable table;
  MsAstNode *root;
  MsAstNode *derived_class;
  MsAstNode *method_decl;
  MsAstNode *method_body;
  MsAstNode *direct_print_stmt;
  MsAstNode *direct_super_expr;
  MsAstNode *inner_decl;
  MsAstNode *inner_print_stmt;
  MsAstNode *nested_super_expr;
  MsResolvedBinding binding;
  MsFunctionResolution method_info;
  MsFunctionResolution inner_info;
  int is_captured = 0;

  root = parse_program(kSource, &arena, &diagnostics);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  derived_class = root->as.program.declarations.items[1];
  TEST_ASSERT(derived_class->kind == MS_AST_CLASS_DECL);
  TEST_ASSERT(derived_class->as.class_decl.methods.count == 1);
  method_decl = derived_class->as.class_decl.methods.items[0];
  TEST_ASSERT(method_decl->kind == MS_AST_FUNCTION_DECL);
  method_body = method_decl->as.function_decl.body;
  TEST_ASSERT(method_body != NULL);
  TEST_ASSERT(method_body->kind == MS_AST_BLOCK);
  TEST_ASSERT(method_body->as.block.statements.count == 2);
  direct_print_stmt = method_body->as.block.statements.items[0];
  TEST_ASSERT(direct_print_stmt->kind == MS_AST_PRINT_STMT);
  direct_super_expr = direct_print_stmt->as.print_stmt.expression;
  TEST_ASSERT(direct_super_expr != NULL);
  TEST_ASSERT(direct_super_expr->kind == MS_AST_SUPER);
  inner_decl = method_body->as.block.statements.items[1];
  TEST_ASSERT(inner_decl->kind == MS_AST_FUNCTION_DECL);
  inner_print_stmt = inner_decl->as.function_decl.body->as.block.statements.items[0];
  TEST_ASSERT(inner_print_stmt->kind == MS_AST_PRINT_STMT);
  nested_super_expr = inner_print_stmt->as.print_stmt.expression;
  TEST_ASSERT(nested_super_expr != NULL);
  TEST_ASSERT(nested_super_expr->kind == MS_AST_SUPER);

  ms_resolution_table_init(&table);
  TEST_ASSERT(ms_resolve_program("<unit>", root, &table, &diagnostics));
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, direct_super_expr->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 1);
  TEST_ASSERT(binding.scope_depth == 1);

  TEST_ASSERT(ms_resolution_table_get(&table, nested_super_expr->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_UPVALUE);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.lexical_depth == 1);
  TEST_ASSERT(binding.function_node_id == inner_decl->node_id);

  TEST_ASSERT(ms_resolution_table_get_function(&table,
                                               method_decl->node_id,
                                               &method_info));
  TEST_ASSERT((method_info.flags & MS_FUNCTION_FLAG_METHOD) != 0);
  TEST_ASSERT((method_info.flags & MS_FUNCTION_FLAG_HAS_SELF) != 0);
  TEST_ASSERT((method_info.flags & MS_FUNCTION_FLAG_HAS_SUPER) != 0);
  TEST_ASSERT(ms_resolution_table_function_local_is_captured(&table,
                                                             method_decl->node_id,
                                                             1,
                                                             &is_captured));
  TEST_ASSERT(is_captured);

  TEST_ASSERT(ms_resolution_table_get_function(&table,
                                               inner_decl->node_id,
                                               &inner_info));
  TEST_ASSERT(inner_info.upvalue_count == 1);
  TEST_ASSERT(inner_info.upvalues[0].is_local == 1);
  TEST_ASSERT(inner_info.upvalues[0].slot == 1);
  TEST_ASSERT(inner_info.upvalues[0].lexical_depth == 1);

  ms_resolution_table_destroy(&table);
  ms_diag_list_destroy(&diagnostics);
  ms_arena_destroy(&arena);
  return 0;
}

static int test_allows_bare_return_inside_initializer(void) {
  static const char kSource[] =
      "class Box {\n"
      "  init() {\n"
      "    return\n"
      "  }\n"
      "}\n";
  MsArena arena;
  MsDiagnosticList diagnostics;
  MsResolutionTable table;
  MsAstNode *root;
  MsAstNode *class_decl;
  MsAstNode *init_decl;
  MsFunctionResolution init_info;

  root = parse_program(kSource, &arena, &diagnostics);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  class_decl = root->as.program.declarations.items[0];
  TEST_ASSERT(class_decl->kind == MS_AST_CLASS_DECL);
  init_decl = class_decl->as.class_decl.methods.items[0];
  TEST_ASSERT(init_decl->kind == MS_AST_FUNCTION_DECL);

  ms_resolution_table_init(&table);
  TEST_ASSERT(ms_resolve_program("<unit>", root, &table, &diagnostics));
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  TEST_ASSERT(ms_resolution_table_get_function(&table,
                                               init_decl->node_id,
                                               &init_info));
  TEST_ASSERT((init_info.flags & MS_FUNCTION_FLAG_METHOD) != 0);
  TEST_ASSERT((init_info.flags & MS_FUNCTION_FLAG_INITIALIZER) != 0);
  TEST_ASSERT((init_info.flags & MS_FUNCTION_FLAG_HAS_SELF) != 0);
  TEST_ASSERT((init_info.flags & MS_FUNCTION_FLAG_HAS_SUPER) == 0);

  ms_resolution_table_destroy(&table);
  ms_diag_list_destroy(&diagnostics);
  ms_arena_destroy(&arena);
  return 0;
}

static int test_resolves_import_bindings_in_global_and_local_scopes(void) {
  static const char kSource[] =
      "import core.io\n"
      "from tools.util import helper as alias\n"
      "{\n"
      "  import pkg.tool as local_tool\n"
      "  from data.values import item\n"
      "  print local_tool\n"
      "  print item\n"
      "}\n"
      "print io\n"
      "print alias\n";
  MsArena arena;
  MsDiagnosticList diagnostics;
  MsResolutionTable table;
  MsAstNode *root;
  MsAstNode *global_import;
  MsAstNode *global_from_import;
  MsAstNode *block_stmt;
  MsAstNode *local_import;
  MsAstNode *local_from_import;
  MsAstNode *local_print_import;
  MsAstNode *local_print_symbol;
  MsAstNode *global_print_import;
  MsAstNode *global_print_alias;
  MsResolvedBinding binding;

  root = parse_program(kSource, &arena, &diagnostics);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  global_import = root->as.program.declarations.items[0];
  global_from_import = root->as.program.declarations.items[1];
  block_stmt = root->as.program.declarations.items[2];
  global_print_import = root->as.program.declarations.items[3];
  global_print_alias = root->as.program.declarations.items[4];
  TEST_ASSERT(global_import->kind == MS_AST_IMPORT_STMT);
  TEST_ASSERT(global_from_import->kind == MS_AST_FROM_IMPORT_STMT);
  TEST_ASSERT(block_stmt->kind == MS_AST_BLOCK);
  TEST_ASSERT(global_print_import->kind == MS_AST_PRINT_STMT);
  TEST_ASSERT(global_print_alias->kind == MS_AST_PRINT_STMT);

  local_import = block_stmt->as.block.statements.items[0];
  local_from_import = block_stmt->as.block.statements.items[1];
  local_print_import = block_stmt->as.block.statements.items[2];
  local_print_symbol = block_stmt->as.block.statements.items[3];
  TEST_ASSERT(local_import->kind == MS_AST_IMPORT_STMT);
  TEST_ASSERT(local_from_import->kind == MS_AST_FROM_IMPORT_STMT);
  TEST_ASSERT(local_print_import->kind == MS_AST_PRINT_STMT);
  TEST_ASSERT(local_print_symbol->kind == MS_AST_PRINT_STMT);

  ms_resolution_table_init(&table);
  TEST_ASSERT(ms_resolve_program("<unit>", root, &table, &diagnostics));
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, global_import->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_GLOBAL);
  TEST_ASSERT(binding.scope_depth == 0);

  TEST_ASSERT(ms_resolution_table_get(&table, global_from_import->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_GLOBAL);
  TEST_ASSERT(binding.scope_depth == 0);

  TEST_ASSERT(ms_resolution_table_get(&table,
                                      global_print_import->as.print_stmt.expression->node_id,
                                      &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_GLOBAL);

  TEST_ASSERT(ms_resolution_table_get(&table,
                                      global_print_alias->as.print_stmt.expression->node_id,
                                      &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_GLOBAL);

  TEST_ASSERT(ms_resolution_table_get(&table, local_import->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 0);
  TEST_ASSERT(binding.scope_depth == 1);

  TEST_ASSERT(ms_resolution_table_get(&table, local_from_import->node_id, &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 1);
  TEST_ASSERT(binding.scope_depth == 1);

  TEST_ASSERT(ms_resolution_table_get(&table,
                                      local_print_import->as.print_stmt.expression->node_id,
                                      &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 0);

  TEST_ASSERT(ms_resolution_table_get(&table,
                                      local_print_symbol->as.print_stmt.expression->node_id,
                                      &binding));
  TEST_ASSERT(binding.kind == MS_BINDING_LOCAL);
  TEST_ASSERT(binding.slot == 1);

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
  TEST_ASSERT(test_resolves_method_self_binding_and_nested_capture() == 0);
  TEST_ASSERT(test_resolves_subclass_super_binding_and_nested_capture() == 0);
  TEST_ASSERT(test_allows_bare_return_inside_initializer() == 0);
  TEST_ASSERT(test_resolves_import_bindings_in_global_and_local_scopes() == 0);
  return 0;
}
