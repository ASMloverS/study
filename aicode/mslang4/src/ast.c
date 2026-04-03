#include "ast.h"
#include "memory.h"
#include <stdlib.h>
#include <string.h>

MsExpr *ms_expr_create(MsExprType type)
{
	MsExpr *expr = MS_ALLOCATE(MsExpr, 1);
	memset(expr, 0, sizeof(MsExpr));
	expr->type = type;
	return expr;
}

void ms_expr_free(MsExpr *expr)
{
	if (expr == NULL)
		return;
	switch (expr->type) {
	case MS_EXPR_ASSIGN:
		ms_expr_free(expr->assign.target);
		ms_expr_free(expr->assign.value);
		break;
	case MS_EXPR_BINARY:
		ms_expr_free(expr->binary.left);
		ms_expr_free(expr->binary.right);
		break;
	case MS_EXPR_CALL:
		ms_expr_free(expr->call.callee);
		if (expr->call.args)
			MS_FREE(MsExpr *, expr->call.args,
				expr->call.arg_count);
		break;
	case MS_EXPR_GET:
		ms_expr_free(expr->get.object);
		break;
	case MS_EXPR_GROUPING:
		ms_expr_free(expr->grouping.expression);
		break;
	case MS_EXPR_LITERAL:
		break;
	case MS_EXPR_LOGICAL:
		ms_expr_free(expr->logical.left);
		ms_expr_free(expr->logical.right);
		break;
	case MS_EXPR_SET:
		ms_expr_free(expr->set.object);
		ms_expr_free(expr->set.value);
		break;
	case MS_EXPR_SUPER:
		break;
	case MS_EXPR_THIS:
		break;
	case MS_EXPR_UNARY:
		ms_expr_free(expr->unary.operand);
		break;
	case MS_EXPR_VARIABLE:
		break;
	case MS_EXPR_LIST:
		if (expr->list.elements) {
			for (int i = 0; i < expr->list.count; i++)
				ms_expr_free(expr->list.elements[i]);
			MS_FREE(MsExpr *, expr->list.elements,
				expr->list.count);
		}
		break;
	case MS_EXPR_SUBSCRIPT:
		ms_expr_free(expr->subscript.object);
		ms_expr_free(expr->subscript.index);
		break;
	}
	MS_FREE(MsExpr, expr, 1);
}

MsStmt *ms_stmt_create(MsStmtType type)
{
	MsStmt *stmt = MS_ALLOCATE(MsStmt, 1);
	memset(stmt, 0, sizeof(MsStmt));
	stmt->type = type;
	return stmt;
}

void ms_stmt_free(MsStmt *stmt)
{
	if (stmt == NULL)
		return;
	switch (stmt->type) {
	case MS_STMT_BLOCK:
		if (stmt->block.stmts) {
			for (int i = 0; i < stmt->block.count; i++)
				ms_stmt_free(stmt->block.stmts[i]);
			MS_FREE(MsStmt *, stmt->block.stmts,
				stmt->block.count);
		}
		break;
	case MS_STMT_CLASS:
		ms_expr_free(stmt->class_stmt.superclass);
		if (stmt->class_stmt.methods) {
			for (int i = 0; i < stmt->class_stmt.method_count; i++)
				ms_stmt_free(stmt->class_stmt.methods[i]);
			MS_FREE(MsStmt *, stmt->class_stmt.methods,
				stmt->class_stmt.method_count);
		}
		break;
	case MS_STMT_EXPRESSION:
		ms_expr_free(stmt->expression);
		break;
	case MS_STMT_FUNCTION:
		if (stmt->function.params)
			MS_FREE(MsToken, stmt->function.params,
				stmt->function.param_count);
		if (stmt->function.body) {
			for (int i = 0; i < stmt->function.body_count; i++)
				ms_stmt_free(stmt->function.body[i]);
			MS_FREE(MsStmt *, stmt->function.body,
				stmt->function.body_count);
		}
		break;
	case MS_STMT_IF:
		ms_expr_free(stmt->if_stmt.condition);
		ms_stmt_free(stmt->if_stmt.then_branch);
		ms_stmt_free(stmt->if_stmt.else_branch);
		break;
	case MS_STMT_IMPORT:
		if (stmt->import.items)
			MS_FREE(MsImportItem, stmt->import.items,
				stmt->import.item_count);
		break;
	case MS_STMT_RETURN:
		ms_expr_free(stmt->return_stmt.value);
		break;
	case MS_STMT_VAR:
		ms_expr_free(stmt->var.initializer);
		break;
	case MS_STMT_WHILE:
		ms_expr_free(stmt->while_stmt.condition);
		ms_stmt_free(stmt->while_stmt.body);
		break;
	case MS_STMT_FOR:
		ms_expr_free(stmt->for_stmt.iterator);
		ms_expr_free(stmt->for_stmt.collection);
		ms_stmt_free(stmt->for_stmt.body);
		break;
	case MS_STMT_BREAK:
		break;
	case MS_STMT_CONTINUE:
		break;
	}
	MS_FREE(MsStmt, stmt, 1);
}

void ms_stmt_list_free(MsStmt **stmts, int count)
{
	if (stmts == NULL)
		return;
	for (int i = 0; i < count; i++)
		ms_stmt_free(stmts[i]);
	MS_FREE(MsStmt *, stmts, count);
}
