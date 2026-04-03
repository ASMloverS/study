#include "parser.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ms_error_at(MsParser *parser, const MsToken *token,
			const char *message)
{
	if (parser->panic_mode)
		return;
	parser->panic_mode = true;
	parser->had_error = true;
	parser->last_error.token = *token;
	snprintf(parser->last_error.message, sizeof(parser->last_error.message),
		 "[line %d] Error: %s", token->line, message);
	fprintf(stderr, "%s\n", parser->last_error.message);
}

static void ms_error(MsParser *parser, const char *message)
{
	ms_error_at(parser, &parser->previous, message);
}

static void ms_error_at_current(MsParser *parser, const char *message)
{
	ms_error_at(parser, &parser->current, message);
}

static void ms_parser_advance(MsParser *parser)
{
	parser->previous = parser->current;
	for (;;) {
		parser->current = ms_scanner_scan_token(&parser->scanner);
		if (parser->current.type != MS_TOKEN_ERROR)
			break;
		ms_error_at_current(parser, parser->current.start);
	}
}

static bool ms_check(MsParser *parser, MsTokenType type)
{
	return parser->current.type == type;
}

static bool ms_match(MsParser *parser, MsTokenType type)
{
	if (!ms_check(parser, type))
		return false;
	ms_parser_advance(parser);
	return true;
}

static void ms_consume(MsParser *parser, MsTokenType type, const char *msg)
{
	if (parser->current.type == type) {
		ms_parser_advance(parser);
		return;
	}
	ms_error_at_current(parser, msg);
}

static MsExpr *ms_alloc_expr(MsExprType type)
{
	MsExpr *expr = MS_ALLOCATE(MsExpr, 1);
	memset(expr, 0, sizeof(MsExpr));
	expr->type = type;
	return expr;
}

static MsStmt *ms_alloc_stmt(MsStmtType type)
{
	MsStmt *stmt = MS_ALLOCATE(MsStmt, 1);
	memset(stmt, 0, sizeof(MsStmt));
	stmt->type = type;
	return stmt;
}

static bool ms_check(MsParser *parser, MsTokenType type);
static MsExpr *ms_parse_expression(MsParser *parser);
static MsExpr *ms_parse_assignment_expr(MsParser *parser);
static MsExpr *ms_parse_or(MsParser *parser);
static MsExpr *ms_parse_and(MsParser *parser);
static MsExpr *ms_parse_equality(MsParser *parser);
static MsExpr *ms_parse_comparison(MsParser *parser);
static MsExpr *ms_parse_term(MsParser *parser);
static MsExpr *ms_parse_factor(MsParser *parser);
static MsExpr *ms_parse_unary(MsParser *parser);
static MsExpr *ms_parse_call(MsParser *parser);
static MsExpr *ms_parse_primary(MsParser *parser);
static MsStmt *ms_parse_statement(MsParser *parser);
static MsStmt *ms_parse_declaration(MsParser *parser);
static MsStmt *ms_parse_block(MsParser *parser);

static MsExpr *ms_parse_call(MsParser *parser)
{
	MsExpr *expr = ms_parse_primary(parser);
	for (;;) {
		if (ms_match(parser, MS_TOKEN_LEFT_PAREN)) {
			MsExpr *call = ms_alloc_expr(MS_EXPR_CALL);
			call->call.callee = expr;
			call->call.paren = parser->previous;
			int cap = 4;
			int count = 0;
			MsExpr **args = MS_ALLOCATE(MsExpr *, cap);
			if (!ms_check(parser, MS_TOKEN_RIGHT_PAREN)) {
				do {
					if (count + 1 > cap) {
						int old = cap;
						cap = MS_GROW_CAPACITY(old);
						args = MS_GROW_ARRAY(MsExpr *, args,
								     old, cap);
					}
					args[count++] = ms_parse_assignment_expr(
						parser);
				} while (ms_match(parser, MS_TOKEN_COMMA));
			}
			ms_consume(parser, MS_TOKEN_RIGHT_PAREN,
				   "Expect ')' after arguments.");
			call->call.args = args;
			call->call.arg_count = count;
			expr = call;
		} else if (ms_match(parser, MS_TOKEN_DOT)) {
			MsExpr *get = ms_alloc_expr(MS_EXPR_GET);
			get->get.object = expr;
			ms_consume(parser, MS_TOKEN_IDENTIFIER,
				   "Expect property name after '.'.");
			get->get.name = parser->previous;
			expr = get;
		} else {
			break;
		}
	}
	return expr;
}

static MsExpr *ms_parse_unary(MsParser *parser)
{
	if (ms_match(parser, MS_TOKEN_MINUS) ||
	    ms_match(parser, MS_TOKEN_BANG)) {
		MsExpr *unary = ms_alloc_expr(MS_EXPR_UNARY);
		unary->unary.op = parser->previous;
		unary->unary.operand = ms_parse_unary(parser);
		return unary;
	}
	return ms_parse_call(parser);
}

static MsExpr *ms_parse_factor(MsParser *parser)
{
	MsExpr *expr = ms_parse_unary(parser);
	while (ms_match(parser, MS_TOKEN_STAR) ||
	       ms_match(parser, MS_TOKEN_SLASH) ||
	       ms_match(parser, MS_TOKEN_PERCENT)) {
		MsExpr *binary = ms_alloc_expr(MS_EXPR_BINARY);
		binary->binary.op = parser->previous;
		binary->binary.left = expr;
		binary->binary.right = ms_parse_unary(parser);
		expr = binary;
	}
	return expr;
}

static MsExpr *ms_parse_term(MsParser *parser)
{
	MsExpr *expr = ms_parse_factor(parser);
	while (ms_match(parser, MS_TOKEN_PLUS) ||
	       ms_match(parser, MS_TOKEN_MINUS)) {
		MsExpr *binary = ms_alloc_expr(MS_EXPR_BINARY);
		binary->binary.op = parser->previous;
		binary->binary.left = expr;
		binary->binary.right = ms_parse_factor(parser);
		expr = binary;
	}
	return expr;
}

static MsExpr *ms_parse_comparison(MsParser *parser)
{
	MsExpr *expr = ms_parse_term(parser);
	while (ms_match(parser, MS_TOKEN_LESS) ||
	       ms_match(parser, MS_TOKEN_LESS_EQUAL) ||
	       ms_match(parser, MS_TOKEN_GREATER) ||
	       ms_match(parser, MS_TOKEN_GREATER_EQUAL)) {
		MsExpr *binary = ms_alloc_expr(MS_EXPR_BINARY);
		binary->binary.op = parser->previous;
		binary->binary.left = expr;
		binary->binary.right = ms_parse_term(parser);
		expr = binary;
	}
	return expr;
}

static MsExpr *ms_parse_equality(MsParser *parser)
{
	MsExpr *expr = ms_parse_comparison(parser);
	while (ms_match(parser, MS_TOKEN_EQUAL_EQUAL) ||
	       ms_match(parser, MS_TOKEN_BANG_EQUAL)) {
		MsExpr *binary = ms_alloc_expr(MS_EXPR_BINARY);
		binary->binary.op = parser->previous;
		binary->binary.left = expr;
		binary->binary.right = ms_parse_comparison(parser);
		expr = binary;
	}
	return expr;
}

static MsExpr *ms_parse_and(MsParser *parser)
{
	MsExpr *expr = ms_parse_equality(parser);
	while (ms_match(parser, MS_TOKEN_AND)) {
		MsExpr *logical = ms_alloc_expr(MS_EXPR_LOGICAL);
		logical->logical.op = parser->previous;
		logical->logical.left = expr;
		logical->logical.right = ms_parse_equality(parser);
		expr = logical;
	}
	return expr;
}

static MsExpr *ms_parse_or(MsParser *parser)
{
	MsExpr *expr = ms_parse_and(parser);
	while (ms_match(parser, MS_TOKEN_OR)) {
		MsExpr *logical = ms_alloc_expr(MS_EXPR_LOGICAL);
		logical->logical.op = parser->previous;
		logical->logical.left = expr;
		logical->logical.right = ms_parse_and(parser);
		expr = logical;
	}
	return expr;
}

static MsExpr *ms_parse_assignment_expr(MsParser *parser)
{
	MsExpr *expr = ms_parse_or(parser);
	if (ms_match(parser, MS_TOKEN_EQUAL)) {
		MsExpr *assign = ms_alloc_expr(MS_EXPR_ASSIGN);
		assign->assign.value = ms_parse_assignment_expr(parser);
		if (expr->type == MS_EXPR_VARIABLE) {
			assign->assign.name = expr->variable;
		} else {
			ms_error(parser, "Invalid assignment target.");
		}
		free(expr);
		return assign;
	}
	return expr;
}

static MsExpr *ms_parse_expression(MsParser *parser)
{
	return ms_parse_assignment_expr(parser);
}

static MsExpr *ms_parse_primary(MsParser *parser)
{
	if (ms_match(parser, MS_TOKEN_NUMBER)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_LITERAL);
		expr->literal.type = MS_LITERAL_NUMBER;
		char buf[64];
		int len = parser->previous.length;
		if (len >= (int)sizeof(buf))
			len = (int)sizeof(buf) - 1;
		memcpy(buf, parser->previous.start, len);
		buf[len] = '\0';
		expr->literal.value.number = strtod(buf, NULL);
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_STRING)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_LITERAL);
		expr->literal.type = MS_LITERAL_STRING;
		expr->literal.value.string_view.start =
			parser->previous.start + 1;
		expr->literal.value.string_view.length =
			parser->previous.length - 2;
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_TRUE)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_LITERAL);
		expr->literal.type = MS_LITERAL_BOOL;
		expr->literal.value.boolean = true;
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_FALSE)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_LITERAL);
		expr->literal.type = MS_LITERAL_BOOL;
		expr->literal.value.boolean = false;
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_NIL)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_LITERAL);
		expr->literal.type = MS_LITERAL_NIL;
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_IDENTIFIER)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_VARIABLE);
		expr->variable = parser->previous;
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_THIS)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_THIS);
		expr->this_expr = parser->previous;
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_SUPER)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_SUPER);
		expr->super.keyword = parser->previous;
		ms_consume(parser, MS_TOKEN_DOT,
			   "Expect '.' after 'super'.");
		ms_consume(parser, MS_TOKEN_IDENTIFIER,
			   "Expect superclass method name.");
		expr->super.method = parser->previous;
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_LEFT_BRACKET)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_LIST);
		int cap = 4;
		int count = 0;
		MsExpr **elements = MS_ALLOCATE(MsExpr *, cap);
		if (!ms_check(parser, MS_TOKEN_RIGHT_BRACKET)) {
			do {
				if (count + 1 > cap) {
					int old = cap;
					cap = MS_GROW_CAPACITY(old);
					elements = MS_GROW_ARRAY(MsExpr *, elements,
								 old, cap);
				}
				elements[count++] = ms_parse_expression(parser);
			} while (ms_match(parser, MS_TOKEN_COMMA));
		}
		ms_consume(parser, MS_TOKEN_RIGHT_BRACKET,
			   "Expect ']' after list elements.");
		expr->list.elements = elements;
		expr->list.count = count;
		return expr;
	}
	if (ms_match(parser, MS_TOKEN_LEFT_PAREN)) {
		MsExpr *expr = ms_alloc_expr(MS_EXPR_GROUPING);
		expr->grouping.expression = ms_parse_expression(parser);
		ms_consume(parser, MS_TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
		return expr;
	}
	ms_error_at_current(parser, "Expect expression.");
	return NULL;
}

static MsStmt *ms_parse_expression_stmt(MsParser *parser)
{
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_EXPRESSION);
	stmt->expression = ms_parse_expression(parser);
	if (parser->current.type == MS_TOKEN_NEWLINE ||
	    parser->current.type == MS_TOKEN_SEMICOLON)
		ms_parser_advance(parser);
	return stmt;
}

static MsStmt *ms_parse_block(MsParser *parser)
{
	ms_consume(parser, MS_TOKEN_LEFT_BRACE, "Expect '{' before block.");
	int cap = 8;
	int count = 0;
	MsStmt **stmts = MS_ALLOCATE(MsStmt *, cap);
	while (!ms_check(parser, MS_TOKEN_RIGHT_BRACE) &&
	       !ms_check(parser, MS_TOKEN_EOF)) {
		if (count + 1 > cap) {
			int old = cap;
			cap = MS_GROW_CAPACITY(old);
			stmts = MS_GROW_ARRAY(MsStmt *, stmts, old, cap);
		}
		MsStmt *s = ms_parse_declaration(parser);
		if (s != NULL)
			stmts[count++] = s;
		if (parser->panic_mode) {
			while (!ms_check(parser, MS_TOKEN_EOF) &&
			       parser->current.type != MS_TOKEN_RIGHT_BRACE &&
			       parser->current.type != MS_TOKEN_NEWLINE)
				ms_parser_advance(parser);
			parser->panic_mode = false;
		}
	}
	ms_consume(parser, MS_TOKEN_RIGHT_BRACE, "Expect '}' after block.");
	MsStmt *block = ms_alloc_stmt(MS_STMT_BLOCK);
	block->block.stmts = stmts;
	block->block.count = count;
	return block;
}

static MsStmt *ms_parse_func_declaration(MsParser *parser)
{
	ms_consume(parser, MS_TOKEN_IDENTIFIER, "Expect function name.");
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_FUNCTION);
	stmt->function.name = parser->previous;
	ms_consume(parser, MS_TOKEN_LEFT_PAREN,
		   "Expect '(' after function name.");
	int param_cap = 4;
	int param_count = 0;
	MsToken *params = MS_ALLOCATE(MsToken, param_cap);
	if (!ms_check(parser, MS_TOKEN_RIGHT_PAREN)) {
		do {
			if (param_count + 1 > param_cap) {
				int old = param_cap;
				param_cap = MS_GROW_CAPACITY(old);
				params = MS_GROW_ARRAY(MsToken, params,
						       old, param_cap);
			}
			ms_consume(parser, MS_TOKEN_IDENTIFIER,
				   "Expect parameter name.");
			params[param_count++] = parser->previous;
		} while (ms_match(parser, MS_TOKEN_COMMA));
	}
	ms_consume(parser, MS_TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
	stmt->function.params = params;
	stmt->function.param_count = param_count;
	if (ms_check(parser, MS_TOKEN_LEFT_BRACE)) {
		MsStmt *body = ms_parse_block(parser);
		stmt->function.body = body->block.stmts;
		stmt->function.body_count = body->block.count;
		free(body);
	} else {
		stmt->function.body = NULL;
		stmt->function.body_count = 0;
	}
	stmt->function.docstring = NULL;
	return stmt;
}

static MsStmt *ms_parse_var_declaration(MsParser *parser)
{
	ms_consume(parser, MS_TOKEN_IDENTIFIER, "Expect variable name.");
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_VAR);
	stmt->var.name = parser->previous;
	if (ms_match(parser, MS_TOKEN_EQUAL))
		stmt->var.initializer = ms_parse_expression(parser);
	if (parser->current.type == MS_TOKEN_NEWLINE ||
	    parser->current.type == MS_TOKEN_SEMICOLON)
		ms_parser_advance(parser);
	return stmt;
}

static MsStmt *ms_parse_if_stmt(MsParser *parser)
{
	ms_consume(parser, MS_TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_IF);
	stmt->if_stmt.condition = ms_parse_expression(parser);
	ms_consume(parser, MS_TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
	stmt->if_stmt.then_branch = ms_parse_declaration(parser);
	if (ms_match(parser, MS_TOKEN_ELSE))
		stmt->if_stmt.else_branch = ms_parse_declaration(parser);
	else
		stmt->if_stmt.else_branch = NULL;
	return stmt;
}

static MsStmt *ms_parse_while_stmt(MsParser *parser)
{
	ms_consume(parser, MS_TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_WHILE);
	stmt->while_stmt.condition = ms_parse_expression(parser);
	ms_consume(parser, MS_TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
	stmt->while_stmt.body = ms_parse_declaration(parser);
	return stmt;
}

static MsStmt *ms_parse_for_stmt(MsParser *parser)
{
	ms_consume(parser, MS_TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_FOR);
	ms_consume(parser, MS_TOKEN_IDENTIFIER, "Expect variable name.");
	stmt->for_stmt.var_name = parser->previous;
	if (parser->current.type == MS_TOKEN_IDENTIFIER &&
	    parser->current.length == 2 &&
	    parser->current.start[0] == 'i' &&
	    parser->current.start[1] == 'n')
		ms_parser_advance(parser);
	else
		ms_error(parser, "Expect 'in' after variable.");
	stmt->for_stmt.collection = ms_parse_expression(parser);
	ms_consume(parser, MS_TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
	stmt->for_stmt.body = ms_parse_declaration(parser);
	stmt->for_stmt.iterator = NULL;
	return stmt;
}

static MsStmt *ms_parse_return_stmt(MsParser *parser)
{
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_RETURN);
	stmt->return_stmt.keyword = parser->previous;
	if (parser->current.type != MS_TOKEN_NEWLINE &&
	    parser->current.type != MS_TOKEN_RIGHT_BRACE &&
	    parser->current.type != MS_TOKEN_EOF)
		stmt->return_stmt.value = ms_parse_expression(parser);
	else
		stmt->return_stmt.value = NULL;
	if (parser->current.type == MS_TOKEN_NEWLINE ||
	    parser->current.type == MS_TOKEN_SEMICOLON)
		ms_parser_advance(parser);
	return stmt;
}

static MsStmt *ms_parse_import_stmt(MsParser *parser)
{
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_IMPORT);
	stmt->import.items = NULL;
	stmt->import.item_count = 0;
	ms_consume(parser, MS_TOKEN_IDENTIFIER,
		   "Expect module name after 'import'.");
	stmt->import.module_path = parser->previous;
	if (parser->current.type == MS_TOKEN_NEWLINE ||
	    parser->current.type == MS_TOKEN_SEMICOLON ||
	    parser->current.type == MS_TOKEN_EOF) {
		if (parser->current.type == MS_TOKEN_NEWLINE ||
		    parser->current.type == MS_TOKEN_SEMICOLON)
			ms_parser_advance(parser);
		return stmt;
	}
	ms_error(parser, "Expect newline or ';' after import.");
	return stmt;
}

static MsStmt *ms_parse_import_from_stmt(MsParser *parser)
{
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_IMPORT);
	ms_consume(parser, MS_TOKEN_IDENTIFIER,
		   "Expect module name after 'from'.");
	stmt->import.module_path = parser->previous;
	ms_consume(parser, MS_TOKEN_IMPORT,
		   "Expect 'import' after module name.");
	int cap = 4;
	int count = 0;
	MsImportItem *items = MS_ALLOCATE(MsImportItem, cap);
	do {
		if (count + 1 > cap) {
			int old = cap;
			cap = MS_GROW_CAPACITY(old);
			items = MS_GROW_ARRAY(MsImportItem, items, old, cap);
		}
		ms_consume(parser, MS_TOKEN_IDENTIFIER,
			   "Expect import item name.");
		items[count].name = parser->previous;
		items[count].alias = parser->previous;
		count++;
	} while (ms_match(parser, MS_TOKEN_COMMA));
	stmt->import.items = items;
	stmt->import.item_count = count;
	if (parser->current.type == MS_TOKEN_NEWLINE ||
	    parser->current.type == MS_TOKEN_SEMICOLON)
		ms_parser_advance(parser);
	return stmt;
}

static MsStmt *ms_parse_class_declaration(MsParser *parser)
{
	ms_consume(parser, MS_TOKEN_IDENTIFIER,
		   "Expect class name after 'class'.");
	MsStmt *stmt = ms_alloc_stmt(MS_STMT_CLASS);
	stmt->class_stmt.name = parser->previous;
	stmt->class_stmt.superclass = NULL;
	if (ms_match(parser, MS_TOKEN_LESS)) {
		ms_consume(parser, MS_TOKEN_IDENTIFIER,
			   "Expect superclass name.");
		MsExpr *sc = ms_alloc_expr(MS_EXPR_VARIABLE);
		sc->variable = parser->previous;
		stmt->class_stmt.superclass = sc;
	}
	ms_consume(parser, MS_TOKEN_LEFT_BRACE,
		   "Expect '{' before class body.");
	int cap = 4;
	int count = 0;
	MsStmt **methods = MS_ALLOCATE(MsStmt *, cap);
	while (!ms_check(parser, MS_TOKEN_RIGHT_BRACE) &&
	       !ms_check(parser, MS_TOKEN_EOF)) {
		if (count + 1 > cap) {
			int old = cap;
			cap = MS_GROW_CAPACITY(old);
			methods = MS_GROW_ARRAY(MsStmt *, methods, old, cap);
		}
		if (ms_match(parser, MS_TOKEN_FN))
			methods[count++] = ms_parse_func_declaration(parser);
		else {
			ms_error_at_current(parser, "Expect method declaration.");
			break;
		}
	}
	ms_consume(parser, MS_TOKEN_RIGHT_BRACE,
		   "Expect '}' after class body.");
	stmt->class_stmt.methods = methods;
	stmt->class_stmt.method_count = count;
	return stmt;
}

static MsStmt *ms_parse_statement(MsParser *parser)
{
	if (ms_check(parser, MS_TOKEN_LEFT_BRACE))
		return ms_parse_block(parser);
	if (ms_match(parser, MS_TOKEN_IF))
		return ms_parse_if_stmt(parser);
	if (ms_match(parser, MS_TOKEN_WHILE))
		return ms_parse_while_stmt(parser);
	if (ms_match(parser, MS_TOKEN_FOR))
		return ms_parse_for_stmt(parser);
	if (ms_match(parser, MS_TOKEN_RETURN))
		return ms_parse_return_stmt(parser);
	if (ms_match(parser, MS_TOKEN_BREAK)) {
		MsStmt *stmt = ms_alloc_stmt(MS_STMT_BREAK);
		stmt->break_stmt = parser->previous;
		if (parser->current.type == MS_TOKEN_NEWLINE ||
		    parser->current.type == MS_TOKEN_SEMICOLON)
			ms_parser_advance(parser);
		return stmt;
	}
	if (ms_match(parser, MS_TOKEN_CONTINUE)) {
		MsStmt *stmt = ms_alloc_stmt(MS_STMT_CONTINUE);
		stmt->continue_stmt = parser->previous;
		if (parser->current.type == MS_TOKEN_NEWLINE ||
		    parser->current.type == MS_TOKEN_SEMICOLON)
			ms_parser_advance(parser);
		return stmt;
	}
	return ms_parse_expression_stmt(parser);
}

static MsStmt *ms_parse_declaration(MsParser *parser)
{
	if (ms_match(parser, MS_TOKEN_VAR))
		return ms_parse_var_declaration(parser);
	if (ms_match(parser, MS_TOKEN_FN))
		return ms_parse_func_declaration(parser);
	if (ms_match(parser, MS_TOKEN_CLASS))
		return ms_parse_class_declaration(parser);
	if (ms_match(parser, MS_TOKEN_IMPORT))
		return ms_parse_import_stmt(parser);
	if (ms_match(parser, MS_TOKEN_FROM))
		return ms_parse_import_from_stmt(parser);
	return ms_parse_statement(parser);
}

void ms_parser_init(MsParser *parser, const char *source)
{
	ms_scanner_init(&parser->scanner, source);
	parser->had_error = false;
	parser->panic_mode = false;
	memset(&parser->current, 0, sizeof(MsToken));
	memset(&parser->previous, 0, sizeof(MsToken));
	memset(&parser->last_error, 0, sizeof(MsParseError));
	ms_parser_advance(parser);
}

int ms_parser_parse(MsParser *parser, MsStmt ***out_statements)
{
	int capacity = 8;
	int count = 0;
	MsStmt **stmts = MS_ALLOCATE(MsStmt *, capacity);

	while (!ms_check(parser, MS_TOKEN_EOF)) {
		MsStmt *stmt = ms_parse_declaration(parser);
		if (parser->panic_mode) {
			while (!ms_check(parser, MS_TOKEN_EOF) &&
			       parser->current.type != MS_TOKEN_NEWLINE &&
			       parser->current.type != MS_TOKEN_RIGHT_BRACE)
				ms_parser_advance(parser);
			if (!ms_check(parser, MS_TOKEN_EOF))
				ms_parser_advance(parser);
			parser->panic_mode = false;
		}
		if (stmt != NULL) {
			if (count + 1 > capacity) {
				int old_cap = capacity;
				capacity = MS_GROW_CAPACITY(old_cap);
				stmts = MS_GROW_ARRAY(MsStmt *, stmts,
						      old_cap, capacity);
			}
			stmts[count++] = stmt;
		}
	}

	if (count == 0) {
		MS_FREE(MsStmt *, stmts, capacity);
		*out_statements = NULL;
		return 0;
	}
	*out_statements = stmts;
	return count;
}

bool ms_parser_had_error(const MsParser *parser)
{
	return parser->had_error;
}
