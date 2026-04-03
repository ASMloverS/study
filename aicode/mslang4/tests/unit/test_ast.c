#include "ast.h"
#include "memory.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static void test_expr_literal(void) {
	MsExpr *expr = ms_expr_create(MS_EXPR_LITERAL);
	assert(expr != NULL);
	assert(expr->type == MS_EXPR_LITERAL);

	expr->literal.type = MS_LITERAL_NUMBER;
	expr->literal.value.number = 42.0;

	MsExpr *expr_nil = ms_expr_create(MS_EXPR_LITERAL);
	assert(expr_nil != NULL);
	expr_nil->literal.type = MS_LITERAL_NIL;

	MsExpr *expr_bool = ms_expr_create(MS_EXPR_LITERAL);
	assert(expr_bool != NULL);
	expr_bool->literal.type = MS_LITERAL_BOOL;
	expr_bool->literal.value.boolean = true;

	ms_expr_free(expr);
	ms_expr_free(expr_nil);
	ms_expr_free(expr_bool);
	printf("  test_expr_literal PASSED\n");
}

static void test_expr_types(void)
{
	MsExpr *left = ms_expr_create(MS_EXPR_LITERAL);
	left->literal.type = MS_LITERAL_NUMBER;
	left->literal.value.number = 1.0;

	MsExpr *right = ms_expr_create(MS_EXPR_LITERAL);
	right->literal.type = MS_LITERAL_NUMBER;
	right->literal.value.number = 2.0;

	MsExpr *binary = ms_expr_create(MS_EXPR_BINARY);
	binary->binary.left = left;
	binary->binary.right = right;
	assert(binary->type == MS_EXPR_BINARY);

	MsExpr *operand = ms_expr_create(MS_EXPR_LITERAL);
	operand->literal.type = MS_LITERAL_NUMBER;
	operand->literal.value.number = 3.0;
	MsExpr *unary = ms_expr_create(MS_EXPR_UNARY);
	unary->unary.operand = operand;

	MsExpr *inner = ms_expr_create(MS_EXPR_LITERAL);
	inner->literal.type = MS_LITERAL_NUMBER;
	inner->literal.value.number = 4.0;
	MsExpr *grouping = ms_expr_create(MS_EXPR_GROUPING);
	grouping->grouping.expression = inner;

	MsExpr *var = ms_expr_create(MS_EXPR_VARIABLE);
	assert(var->type == MS_EXPR_VARIABLE);

	ms_expr_free(binary);
	ms_expr_free(unary);
	ms_expr_free(grouping);
	ms_expr_free(var);
	printf("  test_expr_types PASSED\n");
}

static void test_stmt_basic(void)
{
	MsStmt *expr_stmt = ms_stmt_create(MS_STMT_EXPRESSION);
	assert(expr_stmt != NULL);
	assert(expr_stmt->type == MS_STMT_EXPRESSION);

	MsExpr *lit = ms_expr_create(MS_EXPR_LITERAL);
	lit->literal.type = MS_LITERAL_NUMBER;
	lit->literal.value.number = 1.0;
	expr_stmt->expression = lit;

	MsStmt *var_stmt = ms_stmt_create(MS_STMT_VAR);
	assert(var_stmt != NULL);
	assert(var_stmt->type == MS_STMT_VAR);

	ms_stmt_free(expr_stmt);
	ms_stmt_free(var_stmt);
	printf("  test_stmt_basic PASSED\n");
}

static void test_stmt_types(void)
{
	MsStmt *block = ms_stmt_create(MS_STMT_BLOCK);
	block->block.stmts = MS_ALLOCATE(MsStmt *, 3);
	block->block.count = 3;
	for (int i = 0; i < 3; i++) {
		block->block.stmts[i] = ms_stmt_create(MS_STMT_EXPRESSION);
		MsExpr *lit = ms_expr_create(MS_EXPR_LITERAL);
		lit->literal.type = MS_LITERAL_NUMBER;
		lit->literal.value.number = (double)i;
		block->block.stmts[i]->expression = lit;
	}
	assert(block->type == MS_STMT_BLOCK);

	MsStmt *func = ms_stmt_create(MS_STMT_FUNCTION);
	func->function.param_count = 2;
	func->function.params = MS_ALLOCATE(MsToken, 2);
	func->function.body_count = 1;
	func->function.body = MS_ALLOCATE(MsStmt *, 1);
	func->function.body[0] = ms_stmt_create(MS_STMT_EXPRESSION);
	assert(func->type == MS_STMT_FUNCTION);

	MsStmt *if_stmt = ms_stmt_create(MS_STMT_IF);
	assert(if_stmt->type == MS_STMT_IF);

	MsStmt *while_stmt = ms_stmt_create(MS_STMT_WHILE);
	assert(while_stmt->type == MS_STMT_WHILE);

	MsStmt *import = ms_stmt_create(MS_STMT_IMPORT);
	assert(import->type == MS_STMT_IMPORT);

	ms_stmt_free(block);
	ms_stmt_free(func);
	ms_stmt_free(if_stmt);
	ms_stmt_free(while_stmt);
	ms_stmt_free(import);
	printf("  test_stmt_types PASSED\n");
}

static void test_nested_ast(void)
{
	MsExpr *lit1 = ms_expr_create(MS_EXPR_LITERAL);
	lit1->literal.type = MS_LITERAL_NUMBER;
	lit1->literal.value.number = 1.0;

	MsExpr *lit2 = ms_expr_create(MS_EXPR_LITERAL);
	lit2->literal.type = MS_LITERAL_NUMBER;
	lit2->literal.value.number = 2.0;

	MsExpr *lit3 = ms_expr_create(MS_EXPR_LITERAL);
	lit3->literal.type = MS_LITERAL_NUMBER;
	lit3->literal.value.number = 3.0;

	MsExpr *mul = ms_expr_create(MS_EXPR_BINARY);
	mul->binary.left = lit2;
	mul->binary.right = lit3;

	MsExpr *add = ms_expr_create(MS_EXPR_BINARY);
	add->binary.left = lit1;
	add->binary.right = mul;

	MsStmt *stmt = ms_stmt_create(MS_STMT_EXPRESSION);
	stmt->expression = add;

	ms_stmt_free(stmt);
	printf("  test_nested_ast PASSED\n");
}

static void test_stmt_list_free(void)
{
	ms_stmt_list_free(NULL, 0);

	MsStmt **stmts = MS_ALLOCATE(MsStmt *, 3);
	for (int i = 0; i < 3; i++) {
		stmts[i] = ms_stmt_create(MS_STMT_EXPRESSION);
		MsExpr *lit = ms_expr_create(MS_EXPR_LITERAL);
		lit->literal.type = MS_LITERAL_NUMBER;
		lit->literal.value.number = (double)i;
		stmts[i]->expression = lit;
	}
	ms_stmt_list_free(stmts, 3);
	printf("  test_stmt_list_free PASSED\n");
}

int main(void) {
	printf("Running AST tests...\n");
	test_expr_literal();
	test_expr_types();
	test_stmt_basic();
	test_stmt_types();
	test_nested_ast();
	test_stmt_list_free();
	printf("All AST tests passed.\n");
	return 0;
}
