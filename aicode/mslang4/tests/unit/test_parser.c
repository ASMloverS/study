#include "parser.h"
#include "ast.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_parser_init(void)
{
	MsParser parser;
	ms_parser_init(&parser, "");
	assert(ms_parser_had_error(&parser) == false);

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 0);
	assert(stmts == NULL);

	printf("  test_parser_init PASSED\n");
}

static void test_number_literal(void)
{
	MsParser parser;
	ms_parser_init(&parser, "42");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_EXPRESSION);
	assert(stmts[0]->expression->type == MS_EXPR_LITERAL);
	assert(stmts[0]->expression->literal.type == MS_LITERAL_NUMBER);
	assert(stmts[0]->expression->literal.value.number == 42.0);

	ms_stmt_list_free(stmts, count);
	printf("  test_number_literal PASSED\n");
}

static void test_string_literal(void)
{
	MsParser parser;
	ms_parser_init(&parser, "\"hello\"");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_EXPRESSION);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_LITERAL);
	assert(expr->literal.type == MS_LITERAL_STRING);
	assert(expr->literal.value.string_view.length == 5);
	assert(memcmp(expr->literal.value.string_view.start, "hello", 5) == 0);

	ms_stmt_list_free(stmts, count);
	printf("  test_string_literal PASSED\n");
}

static void test_bool_nil_literals(void)
{
	{
		MsParser parser;
		ms_parser_init(&parser, "true");
		MsStmt **stmts = NULL;
		int count = ms_parser_parse(&parser, &stmts);
		assert(count == 1);
		assert(stmts[0]->expression->type == MS_EXPR_LITERAL);
		assert(stmts[0]->expression->literal.type == MS_LITERAL_BOOL);
		assert(stmts[0]->expression->literal.value.boolean == true);
		ms_stmt_list_free(stmts, count);
	}
	{
		MsParser parser;
		ms_parser_init(&parser, "false");
		MsStmt **stmts = NULL;
		int count = ms_parser_parse(&parser, &stmts);
		assert(count == 1);
		assert(stmts[0]->expression->type == MS_EXPR_LITERAL);
		assert(stmts[0]->expression->literal.type == MS_LITERAL_BOOL);
		assert(stmts[0]->expression->literal.value.boolean == false);
		ms_stmt_list_free(stmts, count);
	}
	{
		MsParser parser;
		ms_parser_init(&parser, "nil");
		MsStmt **stmts = NULL;
		int count = ms_parser_parse(&parser, &stmts);
		assert(count == 1);
		assert(stmts[0]->expression->type == MS_EXPR_LITERAL);
		assert(stmts[0]->expression->literal.type == MS_LITERAL_NIL);
		ms_stmt_list_free(stmts, count);
	}
	printf("  test_bool_nil_literals PASSED\n");
}

static void test_grouping(void)
{
	MsParser parser;
	ms_parser_init(&parser, "(1 + 2)");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_EXPRESSION);
	assert(stmts[0]->expression->type == MS_EXPR_GROUPING);
	assert(stmts[0]->expression->grouping.expression != NULL);

	ms_stmt_list_free(stmts, count);
	printf("  test_grouping PASSED\n");
}

static void test_unary_negate(void)
{
	MsParser parser;
	ms_parser_init(&parser, "-42");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_EXPRESSION);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_UNARY);
	assert(expr->unary.op.type == MS_TOKEN_MINUS);
	assert(expr->unary.operand->type == MS_EXPR_LITERAL);
	assert(expr->unary.operand->literal.value.number == 42.0);

	ms_stmt_list_free(stmts, count);
	printf("  test_unary_negate PASSED\n");
}

static void test_unary_not(void)
{
	MsParser parser;
	ms_parser_init(&parser, "!true");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_UNARY);
	assert(expr->unary.op.type == MS_TOKEN_BANG);

	ms_stmt_list_free(stmts, count);
	printf("  test_unary_not PASSED\n");
}

static void test_binary_add(void)
{
	MsParser parser;
	ms_parser_init(&parser, "1 + 2");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_BINARY);
	assert(expr->binary.op.type == MS_TOKEN_PLUS);
	assert(expr->binary.left->type == MS_EXPR_LITERAL);
	assert(expr->binary.left->literal.value.number == 1.0);
	assert(expr->binary.right->type == MS_EXPR_LITERAL);
	assert(expr->binary.right->literal.value.number == 2.0);

	ms_stmt_list_free(stmts, count);
	printf("  test_binary_add PASSED\n");
}

static void test_precedence(void)
{
	MsParser parser;
	ms_parser_init(&parser, "1 + 2 * 3");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_BINARY);
	assert(expr->binary.op.type == MS_TOKEN_PLUS);
	assert(expr->binary.left->literal.value.number == 1.0);
	assert(expr->binary.right->type == MS_EXPR_BINARY);
	assert(expr->binary.right->binary.op.type == MS_TOKEN_STAR);
	assert(expr->binary.right->binary.left->literal.value.number == 2.0);
	assert(expr->binary.right->binary.right->literal.value.number == 3.0);

	ms_stmt_list_free(stmts, count);
	printf("  test_precedence PASSED\n");
}

static void test_left_associativity(void)
{
	MsParser parser;
	ms_parser_init(&parser, "1 - 2 - 3");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_BINARY);
	assert(expr->binary.op.type == MS_TOKEN_MINUS);
	assert(expr->binary.left->type == MS_EXPR_BINARY);
	assert(expr->binary.left->binary.op.type == MS_TOKEN_MINUS);
	assert(expr->binary.left->binary.left->literal.value.number == 1.0);
	assert(expr->binary.left->binary.right->literal.value.number == 2.0);
	assert(expr->binary.right->literal.value.number == 3.0);

	ms_stmt_list_free(stmts, count);
	printf("  test_left_associativity PASSED\n");
}

static void test_comparison(void)
{
	MsParser parser;
	ms_parser_init(&parser, "1 < 2");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_BINARY);
	assert(expr->binary.op.type == MS_TOKEN_LESS);
	ms_stmt_list_free(stmts, count);
	printf("  test_comparison PASSED\n");
}

static void test_equality(void)
{
	MsParser parser;
	ms_parser_init(&parser, "1 == 2");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_BINARY);
	assert(expr->binary.op.type == MS_TOKEN_EQUAL_EQUAL);
	ms_stmt_list_free(stmts, count);

	ms_parser_init(&parser, "1 != 2");
	stmts = NULL;
	count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->expression->binary.op.type == MS_TOKEN_BANG_EQUAL);
	ms_stmt_list_free(stmts, count);
	printf("  test_equality PASSED\n");
}

static void test_logical_and(void)
{
	MsParser parser;
	ms_parser_init(&parser, "true and false");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_LOGICAL);
	assert(expr->logical.op.type == MS_TOKEN_AND);
	ms_stmt_list_free(stmts, count);
	printf("  test_logical_and PASSED\n");
}

static void test_logical_or(void)
{
	MsParser parser;
	ms_parser_init(&parser, "true or false");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_LOGICAL);
	assert(expr->logical.op.type == MS_TOKEN_OR);
	ms_stmt_list_free(stmts, count);
	printf("  test_logical_or PASSED\n");
}

static void test_mixed_precedence(void)
{
	MsParser parser;
	ms_parser_init(&parser, "1 + 2 < 3 * 4");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_BINARY);
	assert(expr->binary.op.type == MS_TOKEN_LESS);
	assert(expr->binary.left->type == MS_EXPR_BINARY);
	assert(expr->binary.left->binary.op.type == MS_TOKEN_PLUS);
	assert(expr->binary.right->type == MS_EXPR_BINARY);
	assert(expr->binary.right->binary.op.type == MS_TOKEN_STAR);
	ms_stmt_list_free(stmts, count);
	printf("  test_mixed_precedence PASSED\n");
}

static void test_variable_reference(void)
{
	MsParser parser;
	ms_parser_init(&parser, "x");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_VARIABLE);
	assert(expr->variable.length == 1);
	assert(expr->variable.start[0] == 'x');
	ms_stmt_list_free(stmts, count);
	printf("  test_variable_reference PASSED\n");
}

static void test_assignment(void)
{
	MsParser parser;
	ms_parser_init(&parser, "x = 42");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_ASSIGN);
	assert(expr->assign.name.length == 1);
	assert(expr->assign.name.start[0] == 'x');
	assert(expr->assign.value->type == MS_EXPR_LITERAL);
	assert(expr->assign.value->literal.value.number == 42.0);
	ms_stmt_list_free(stmts, count);
	printf("  test_assignment PASSED\n");
}

static void test_compound_expression(void)
{
	MsParser parser;
	ms_parser_init(&parser, "x + y");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_BINARY);
	assert(expr->binary.op.type == MS_TOKEN_PLUS);
	assert(expr->binary.left->type == MS_EXPR_VARIABLE);
	assert(expr->binary.right->type == MS_EXPR_VARIABLE);
	ms_stmt_list_free(stmts, count);
	printf("  test_compound_expression PASSED\n");
}

static void test_var_decl_no_init(void)
{
	MsParser parser;
	ms_parser_init(&parser, "var x");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_VAR);
	assert(stmts[0]->var.name.length == 1);
	assert(stmts[0]->var.name.start[0] == 'x');
	assert(stmts[0]->var.initializer == NULL);
	ms_stmt_list_free(stmts, count);
	printf("  test_var_decl_no_init PASSED\n");
}

static void test_var_decl_with_init(void)
{
	MsParser parser;
	ms_parser_init(&parser, "var x = 42");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_VAR);
	assert(stmts[0]->var.initializer != NULL);
	assert(stmts[0]->var.initializer->type == MS_EXPR_LITERAL);
	assert(stmts[0]->var.initializer->literal.value.number == 42.0);
	ms_stmt_list_free(stmts, count);
	printf("  test_var_decl_with_init PASSED\n");
}

static void test_block(void)
{
	MsParser parser;
	ms_parser_init(&parser, "{ var x = 1\n var y = 2 }");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_BLOCK);
	assert(stmts[0]->block.count == 2);
	assert(stmts[0]->block.stmts[0]->type == MS_STMT_VAR);
	assert(stmts[0]->block.stmts[1]->type == MS_STMT_VAR);
	ms_stmt_list_free(stmts, count);
	printf("  test_block PASSED\n");
}

static void test_empty_block(void)
{
	MsParser parser;
	ms_parser_init(&parser, "{}");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_BLOCK);
	assert(stmts[0]->block.count == 0);
	ms_stmt_list_free(stmts, count);
	printf("  test_empty_block PASSED\n");
}

static void test_if_statement(void)
{
	MsParser parser;
	ms_parser_init(&parser, "if (true) 1");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_IF);
	assert(stmts[0]->if_stmt.condition->type == MS_EXPR_LITERAL);
	assert(stmts[0]->if_stmt.then_branch != NULL);
	assert(stmts[0]->if_stmt.else_branch == NULL);
	ms_stmt_list_free(stmts, count);
	printf("  test_if_statement PASSED\n");
}

static void test_if_else(void)
{
	MsParser parser;
	ms_parser_init(&parser, "if (true) 1 else 2");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_IF);
	assert(stmts[0]->if_stmt.then_branch != NULL);
	assert(stmts[0]->if_stmt.else_branch != NULL);
	ms_stmt_list_free(stmts, count);
	printf("  test_if_else PASSED\n");
}

static void test_while_statement(void)
{
	MsParser parser;
	ms_parser_init(&parser, "while (true) 1");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_WHILE);
	assert(stmts[0]->while_stmt.condition != NULL);
	assert(stmts[0]->while_stmt.body != NULL);
	ms_stmt_list_free(stmts, count);
	printf("  test_while_statement PASSED\n");
}

static void test_for_statement(void)
{
	MsParser parser;
	ms_parser_init(&parser, "for (i in items) 1");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_FOR);
	assert(stmts[0]->for_stmt.var_name.length == 1);
	assert(stmts[0]->for_stmt.var_name.start[0] == 'i');
	assert(stmts[0]->for_stmt.collection != NULL);
	assert(stmts[0]->for_stmt.body != NULL);
	ms_stmt_list_free(stmts, count);
	printf("  test_for_statement PASSED\n");
}

static void test_return_statement(void)
{
	MsParser parser;
	ms_parser_init(&parser, "return 42");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_RETURN);
	assert(stmts[0]->return_stmt.value != NULL);
	assert(stmts[0]->return_stmt.value->literal.value.number == 42.0);
	ms_stmt_list_free(stmts, count);
	printf("  test_return_statement PASSED\n");
}

static void test_break_continue(void)
{
	{
		MsParser parser;
		ms_parser_init(&parser, "break");
		MsStmt **stmts = NULL;
		int count = ms_parser_parse(&parser, &stmts);
		assert(count == 1);
		assert(stmts[0]->type == MS_STMT_BREAK);
		ms_stmt_list_free(stmts, count);
	}
	{
		MsParser parser;
		ms_parser_init(&parser, "continue");
		MsStmt **stmts = NULL;
		int count = ms_parser_parse(&parser, &stmts);
		assert(count == 1);
		assert(stmts[0]->type == MS_STMT_CONTINUE);
		ms_stmt_list_free(stmts, count);
	}
	printf("  test_break_continue PASSED\n");
}

static void test_function_decl(void)
{
	MsParser parser;
	ms_parser_init(&parser, "fn add(a, b) { return a + b }");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_FUNCTION);
	assert(stmts[0]->function.name.length == 3);
	assert(stmts[0]->function.param_count == 2);
	assert(stmts[0]->function.body_count == 1);
	assert(stmts[0]->function.body[0]->type == MS_STMT_RETURN);
	ms_stmt_list_free(stmts, count);
	printf("  test_function_decl PASSED\n");
}

static void test_function_no_params(void)
{
	MsParser parser;
	ms_parser_init(&parser, "fn foo() { 1 }");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_FUNCTION);
	assert(stmts[0]->function.param_count == 0);
	assert(stmts[0]->function.body_count == 1);
	ms_stmt_list_free(stmts, count);
	printf("  test_function_no_params PASSED\n");
}

static void test_function_empty_body(void)
{
	MsParser parser;
	ms_parser_init(&parser, "fn noop() {}");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_FUNCTION);
	assert(stmts[0]->function.body_count == 0);
	ms_stmt_list_free(stmts, count);
	printf("  test_function_empty_body PASSED\n");
}

static void test_function_call(void)
{
	MsParser parser;
	ms_parser_init(&parser, "add(1, 2)");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_CALL);
	assert(expr->call.callee->type == MS_EXPR_VARIABLE);
	assert(expr->call.arg_count == 2);
	ms_stmt_list_free(stmts, count);
	printf("  test_function_call PASSED\n");
}

static void test_class_decl(void)
{
	MsParser parser;
	ms_parser_init(&parser, "class Foo { fn bar() { 1 } }");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_CLASS);
	assert(stmts[0]->class_stmt.name.length == 3);
	assert(stmts[0]->class_stmt.method_count == 1);
	assert(stmts[0]->class_stmt.superclass == NULL);
	ms_stmt_list_free(stmts, count);
	printf("  test_class_decl PASSED\n");
}

static void test_class_inheritance(void)
{
	MsParser parser;
	ms_parser_init(&parser, "class Child < Parent { }");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_CLASS);
	assert(stmts[0]->class_stmt.superclass != NULL);
	assert(stmts[0]->class_stmt.superclass->type == MS_EXPR_VARIABLE);
	ms_stmt_list_free(stmts, count);
	printf("  test_class_inheritance PASSED\n");
}

static void test_import_simple(void)
{
	MsParser parser;
	ms_parser_init(&parser, "import math");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_IMPORT);
	assert(stmts[0]->import.module_path.length == 4);
	assert(stmts[0]->import.item_count == 0);
	ms_stmt_list_free(stmts, count);
	printf("  test_import_simple PASSED\n");
}

static void test_import_from(void)
{
	MsParser parser;
	ms_parser_init(&parser, "from math import sqrt");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	assert(stmts[0]->type == MS_STMT_IMPORT);
	assert(stmts[0]->import.module_path.length == 4);
	assert(stmts[0]->import.item_count == 1);
	ms_stmt_list_free(stmts, count);
	printf("  test_import_from PASSED\n");
}

static void test_this_super(void)
{
	{
		MsParser parser;
		ms_parser_init(&parser, "this");
		MsStmt **stmts = NULL;
		int count = ms_parser_parse(&parser, &stmts);
		assert(count == 1);
		assert(stmts[0]->expression->type == MS_EXPR_THIS);
		ms_stmt_list_free(stmts, count);
	}
	{
		MsParser parser;
		ms_parser_init(&parser, "super.method");
		MsStmt **stmts = NULL;
		int count = ms_parser_parse(&parser, &stmts);
		assert(count == 1);
		assert(stmts[0]->expression->type == MS_EXPR_SUPER);
		assert(stmts[0]->expression->super.method.length == 6);
		ms_stmt_list_free(stmts, count);
	}
	printf("  test_this_super PASSED\n");
}

static void test_property_access(void)
{
	MsParser parser;
	ms_parser_init(&parser, "obj.x");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_GET);
	assert(expr->get.object->type == MS_EXPR_VARIABLE);
	assert(expr->get.name.length == 1);
	ms_stmt_list_free(stmts, count);
	printf("  test_property_access PASSED\n");
}

static void test_list_literal(void)
{
	MsParser parser;
	ms_parser_init(&parser, "[1, 2, 3]");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 1);
	MsExpr *expr = stmts[0]->expression;
	assert(expr->type == MS_EXPR_LIST);
	assert(expr->list.count == 3);
	ms_stmt_list_free(stmts, count);
	printf("  test_list_literal PASSED\n");
}

static void test_error_missing_semicolon(void)
{
	MsParser parser;
	ms_parser_init(&parser, "var x = 42\nvar y = 1");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(count == 2);
	assert(stmts[0]->type == MS_STMT_VAR);
	assert(stmts[1]->type == MS_STMT_VAR);
	ms_stmt_list_free(stmts, count);
	printf("  test_error_missing_semicolon PASSED\n");
}

static void test_error_unclosed_brace(void)
{
	MsParser parser;
	ms_parser_init(&parser, "{ var x = 1");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(ms_parser_had_error(&parser) == true);
	assert(count >= 1);
	ms_stmt_list_free(stmts, count);
	printf("  test_error_unclosed_brace PASSED\n");
}

static void test_error_invalid_token(void)
{
	MsParser parser;
	ms_parser_init(&parser, "var 42 = x");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(ms_parser_had_error(&parser) == true);
	assert(parser.last_error.token.line == 1);
	ms_stmt_list_free(stmts, count);
	printf("  test_error_invalid_token PASSED\n");
}

static void test_error_multiple(void)
{
	MsParser parser;
	ms_parser_init(&parser, "var x =\nvar y = 1 + \n1");

	MsStmt **stmts = NULL;
	int count = ms_parser_parse(&parser, &stmts);
	assert(ms_parser_had_error(&parser) == true);
	assert(count >= 1);
	ms_stmt_list_free(stmts, count);
	printf("  test_error_multiple PASSED\n");
}

static void test_had_error_flag(void)
{
	{
		MsParser parser;
		ms_parser_init(&parser, "42");
		MsStmt **stmts = NULL;
		ms_parser_parse(&parser, &stmts);
		assert(ms_parser_had_error(&parser) == false);
		ms_stmt_list_free(stmts, 1);
	}
	{
		MsParser parser;
		ms_parser_init(&parser, "var 42 = x");
		MsStmt **stmts = NULL;
		ms_parser_parse(&parser, &stmts);
		assert(ms_parser_had_error(&parser) == true);
		ms_stmt_list_free(stmts, 0);
	}
	printf("  test_had_error_flag PASSED\n");
}

int main(void)
{
	printf("Running parser tests...\n");
	test_parser_init();
	test_number_literal();
	test_string_literal();
	test_bool_nil_literals();
	test_grouping();
	test_unary_negate();
	test_unary_not();
	test_binary_add();
	test_precedence();
	test_left_associativity();
	test_comparison();
	test_equality();
	test_logical_and();
	test_logical_or();
	test_mixed_precedence();
	test_variable_reference();
	test_assignment();
	test_compound_expression();
	test_var_decl_no_init();
	test_var_decl_with_init();
	test_block();
	test_empty_block();
	test_if_statement();
	test_if_else();
	test_while_statement();
	test_for_statement();
	test_return_statement();
	test_break_continue();
	test_function_decl();
	test_function_no_params();
	test_function_empty_body();
	test_function_call();
	test_class_decl();
	test_class_inheritance();
	test_import_simple();
	test_import_from();
	test_this_super();
	test_property_access();
	test_list_literal();
	test_error_missing_semicolon();
	test_error_unclosed_brace();
	test_error_invalid_token();
	test_error_multiple();
	test_had_error_flag();
	printf("All parser tests passed.\n");
	return 0;
}
