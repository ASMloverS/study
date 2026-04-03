#ifndef MS_AST_H
#define MS_AST_H

#include "common.h"
#include "token.h"

typedef enum {
	MS_EXPR_ASSIGN, MS_EXPR_BINARY, MS_EXPR_CALL, MS_EXPR_GET,
	MS_EXPR_GROUPING, MS_EXPR_LITERAL, MS_EXPR_LOGICAL, MS_EXPR_SET,
	MS_EXPR_SUPER, MS_EXPR_THIS, MS_EXPR_UNARY, MS_EXPR_VARIABLE,
	MS_EXPR_LIST, MS_EXPR_SUBSCRIPT
} MsExprType;

typedef enum {
	MS_LITERAL_NIL, MS_LITERAL_BOOL, MS_LITERAL_NUMBER, MS_LITERAL_STRING
} MsLiteralType;

typedef struct {
	MsLiteralType type;
	union {
		bool boolean;
		double number;
		struct {
			const char *start;
			int length;
		} string_view;
	} value;
} MsLiteralValue;

typedef struct MsExpr {
	MsExprType type;
	union {
		struct {
			struct MsExpr *target;
			MsToken name;
			struct MsExpr *value;
		} assign;
		struct {
			MsToken op;
			struct MsExpr *left;
			struct MsExpr *right;
		} binary;
		struct {
			struct MsExpr *callee;
			struct MsExpr **args;
			int arg_count;
			MsToken paren;
		} call;
		struct {
			struct MsExpr *object;
			MsToken name;
		} get;
		struct {
			struct MsExpr *expression;
		} grouping;
		MsLiteralValue literal;
		struct {
			MsToken op;
			struct MsExpr *left;
			struct MsExpr *right;
		} logical;
		struct {
			struct MsExpr *object;
			MsToken name;
			struct MsExpr *value;
		} set;
		struct {
			MsToken keyword;
			MsToken method;
		} super;
		MsToken this_expr;
		struct {
			MsToken op;
			struct MsExpr *operand;
		} unary;
		MsToken variable;
		struct {
			struct MsExpr **elements;
			int count;
		} list;
		struct {
			struct MsExpr *object;
			struct MsExpr *index;
			MsToken bracket;
		} subscript;
	};
} MsExpr;

MsExpr *ms_expr_create(MsExprType type);
void ms_expr_free(MsExpr *expr);

typedef enum {
	MS_STMT_BLOCK, MS_STMT_CLASS, MS_STMT_EXPRESSION, MS_STMT_FUNCTION,
	MS_STMT_IF, MS_STMT_IMPORT, MS_STMT_RETURN, MS_STMT_VAR,
	MS_STMT_WHILE, MS_STMT_FOR, MS_STMT_BREAK, MS_STMT_CONTINUE
} MsStmtType;

typedef struct MsString MsString;
typedef struct {
	MsToken name;
	MsToken alias;
} MsImportItem;

struct MsStmt {
	MsStmtType type;
	union {
		struct {
			struct MsStmt **stmts;
			int count;
		} block;
		struct {
			MsToken name;
			struct MsStmt **methods;
			int method_count;
			struct MsExpr *superclass;
		} class_stmt;
		struct MsExpr *expression;
		struct {
			MsToken name;
			MsToken *params;
			int param_count;
			struct MsStmt **body;
			int body_count;
			MsString *docstring;
		} function;
		struct {
			struct MsExpr *condition;
			struct MsStmt *then_branch;
			struct MsStmt *else_branch;
		} if_stmt;
		struct {
			MsImportItem *items;
			int item_count;
			MsToken module_path;
		} import;
		struct {
			MsToken keyword;
			struct MsExpr *value;
		} return_stmt;
		struct {
			MsToken name;
			struct MsExpr *initializer;
		} var;
		struct {
			struct MsExpr *condition;
			struct MsStmt *body;
		} while_stmt;
		struct {
			MsToken var_name;
			struct MsExpr *iterator;
			struct MsExpr *collection;
			struct MsStmt *body;
		} for_stmt;
		MsToken break_stmt;
		MsToken continue_stmt;
	};
};

typedef struct MsStmt MsStmt;

MsStmt *ms_stmt_create(MsStmtType type);
void ms_stmt_free(MsStmt *stmt);
void ms_stmt_list_free(MsStmt **stmts, int count);

#endif
