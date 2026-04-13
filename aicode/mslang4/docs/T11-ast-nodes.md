# T11: AST Nodes

**Phase**: 4 — AST & Parser
**Deps**: T06 (Token Types)
**Complexity**: Medium

## Goal

AST node types for expressions + statements. Create/free fns. Parser output → compiler input.

## Files

| File | Purpose |
|------|---------|
| `src/ast.h` | AST enums, node structs, create/free decls |
| `src/ast.c` | Node creation + recursive free |

## TDD Cycles

### Cycle 1: Expr Create/Free (Literal)

**RED** — `tests/unit/test_ast.c` → `test_expr_literal`:
- `ms_expr_create(MS_EXPR_LITERAL)` → non-NULL, correct type
- Set literal type + value
- Free → no crash

```c
#include "ast.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static void test_expr_literal(void) {
    MsExpr* expr = ms_expr_create(MS_EXPR_LITERAL);
    assert(expr != NULL);
    assert(expr->type == MS_EXPR_LITERAL);
    expr->literal.type = MS_LITERAL_NUMBER;
    expr->literal.value.number = 42.0;

    MsExpr* expr_nil = ms_expr_create(MS_EXPR_LITERAL);
    assert(expr_nil != NULL);
    expr_nil->literal.type = MS_LITERAL_NIL;

    MsExpr* expr_bool = ms_expr_create(MS_EXPR_LITERAL);
    assert(expr_bool != NULL);
    expr_bool->literal.type = MS_LITERAL_BOOL;
    expr_bool->literal.value.boolean = true;

    ms_expr_free(expr);
    ms_expr_free(expr_nil);
    ms_expr_free(expr_bool);
    printf("  test_expr_literal PASSED\n");
}

int main(void) {
    printf("Running AST tests...\n");
    test_expr_literal();
    printf("All AST tests passed.\n");
    return 0;
}
```

**Verify RED**: `gcc -I src -o build/test_ast tests/unit/test_ast.c src/ast.c` → `ast.h: No such file or directory`

**GREEN** → `src/ast.h`:

```c
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

typedef enum { MS_LITERAL_NIL, MS_LITERAL_BOOL, MS_LITERAL_NUMBER, MS_LITERAL_STRING } MsLiteralType;

typedef struct {
    MsLiteralType type;
    union { bool boolean; double number; struct { const char* start; int length; } string_view; } value;
} MsLiteralValue;

typedef struct MsExpr {
    MsExprType type;
    union {
        struct { struct MsExpr* target; MsToken name; struct MsExpr* value; } assign;
        struct { MsToken op; struct MsExpr* left; struct MsExpr* right; } binary;
        struct { struct MsExpr* callee; struct MsExpr** args; int arg_count; MsToken paren; } call;
        struct { struct MsExpr* object; MsToken name; } get;
        struct { struct MsExpr* expression; } grouping;
        MsLiteralValue literal;
        struct { MsToken op; struct MsExpr* left; struct MsExpr* right; } logical;
        struct { struct MsExpr* object; MsToken name; struct MsExpr* value; } set;
        struct { MsToken keyword; MsToken method; } super;
        MsToken this_expr;
        struct { MsToken op; struct MsExpr* operand; } unary;
        MsToken variable;
        struct { struct MsExpr** elements; int count; } list;
        struct { struct MsExpr* object; struct MsExpr* index; MsToken bracket; } subscript;
    };
} MsExpr;

MsExpr* ms_expr_create(MsExprType type);
void ms_expr_free(MsExpr* expr);

typedef struct MsStmt MsStmt;
typedef struct { MsToken name; MsToken alias; } MsImportItem;

void ms_stmt_free(MsStmt* stmt);
void ms_stmt_list_free(MsStmt** stmts, int count);

#endif
```

`src/ast.c`:

```c
#include "ast.h"
#include <stdlib.h>
#include <string.h>

MsExpr* ms_expr_create(MsExprType type) {
    MsExpr* expr = MS_ALLOCATE(MsExpr, 1);
    memset(expr, 0, sizeof(MsExpr));
    expr->type = type;
    return expr;
}

void ms_expr_free(MsExpr* expr) {
    if (expr == NULL) return;
    switch (expr->type) {
        case MS_EXPR_ASSIGN: ms_expr_free(expr->assign.target); ms_expr_free(expr->assign.value); break;
        case MS_EXPR_BINARY: ms_expr_free(expr->binary.left); ms_expr_free(expr->binary.right); break;
        case MS_EXPR_CALL:
            ms_expr_free(expr->call.callee);
            if (expr->call.args) MS_FREE(MsExpr*, expr->call.args, expr->call.arg_count);
            break;
        case MS_EXPR_GET: ms_expr_free(expr->get.object); break;
        case MS_EXPR_GROUPING: ms_expr_free(expr->grouping.expression); break;
        case MS_EXPR_LITERAL: break;
        case MS_EXPR_LOGICAL: ms_expr_free(expr->logical.left); ms_expr_free(expr->logical.right); break;
        case MS_EXPR_SET: ms_expr_free(expr->set.object); ms_expr_free(expr->set.value); break;
        case MS_EXPR_SUPER: break;
        case MS_EXPR_THIS: break;
        case MS_EXPR_UNARY: ms_expr_free(expr->unary.operand); break;
        case MS_EXPR_VARIABLE: break;
        case MS_EXPR_LIST:
            if (expr->list.elements) {
                for (int i = 0; i < expr->list.count; i++) ms_expr_free(expr->list.elements[i]);
                MS_FREE(MsExpr*, expr->list.elements, expr->list.count);
            }
            break;
        case MS_EXPR_SUBSCRIPT: ms_expr_free(expr->subscript.object); ms_expr_free(expr->subscript.index); break;
    }
    MS_FREE(MsExpr, expr, 1);
}
```

**Verify GREEN**: build + run → pass

**REFACTOR**: none

---

### Cycle 2: All Expr Types (Binary, Unary, Grouping, Variable)

**RED** → `test_expr_types`:

```c
static void test_expr_types(void) {
    MsExpr* left = ms_expr_create(MS_EXPR_LITERAL);
    left->literal.type = MS_LITERAL_NUMBER;
    left->literal.value.number = 1.0;
    MsExpr* right = ms_expr_create(MS_EXPR_LITERAL);
    right->literal.type = MS_LITERAL_NUMBER;
    right->literal.value.number = 2.0;
    MsExpr* binary = ms_expr_create(MS_EXPR_BINARY);
    binary->binary.left = left;
    binary->binary.right = right;
    assert(binary->type == MS_EXPR_BINARY);

    MsExpr* operand = ms_expr_create(MS_EXPR_LITERAL);
    operand->literal.type = MS_LITERAL_NUMBER;
    operand->literal.value.number = 3.0;
    MsExpr* unary = ms_expr_create(MS_EXPR_UNARY);
    unary->unary.operand = operand;

    MsExpr* inner = ms_expr_create(MS_EXPR_LITERAL);
    inner->literal.type = MS_LITERAL_NUMBER;
    inner->literal.value.number = 4.0;
    MsExpr* grouping = ms_expr_create(MS_EXPR_GROUPING);
    grouping->grouping.expression = inner;

    MsExpr* var = ms_expr_create(MS_EXPR_VARIABLE);
    assert(var->type == MS_EXPR_VARIABLE);

    ms_expr_free(binary);
    ms_expr_free(unary);
    ms_expr_free(grouping);
    ms_expr_free(var);
    printf("  test_expr_types PASSED\n");
}
```

**Verify GREEN**: builds + passes (types + free from Cycle 1). Verification cycle.

**REFACTOR**: none

---

### Cycle 3: Stmt Create/Free (Expression + Var)

**RED** → `test_stmt_basic`:

```c
static void test_stmt_basic(void) {
    MsStmt* expr_stmt = ms_stmt_create(MS_STMT_EXPRESSION);
    assert(expr_stmt != NULL);
    assert(expr_stmt->type == MS_STMT_EXPRESSION);
    MsExpr* lit = ms_expr_create(MS_EXPR_LITERAL);
    lit->literal.type = MS_LITERAL_NUMBER;
    lit->literal.value.number = 1.0;
    expr_stmt->expression = lit;

    MsStmt* var_stmt = ms_stmt_create(MS_STMT_VAR);
    assert(var_stmt != NULL);
    assert(var_stmt->type == MS_STMT_VAR);

    ms_stmt_free(expr_stmt);
    ms_stmt_free(var_stmt);
    printf("  test_stmt_basic PASSED\n");
}
```

**Verify RED**: `MsStmt`, `ms_stmt_create` undeclared.

**GREEN** → add to `src/ast.h`:

```c
typedef enum {
    MS_STMT_BLOCK, MS_STMT_CLASS, MS_STMT_EXPRESSION, MS_STMT_FUNCTION,
    MS_STMT_IF, MS_STMT_IMPORT, MS_STMT_RETURN, MS_STMT_VAR,
    MS_STMT_WHILE, MS_STMT_FOR, MS_STMT_BREAK, MS_STMT_CONTINUE
} MsStmtType;

struct MsStmt {
    MsStmtType type;
    union {
        struct { MsStmt** stmts; int count; } block;
        struct { MsToken name; MsStmt** methods; int method_count; struct MsExpr* superclass; } class_stmt;
        struct MsExpr* expression;
        struct { MsToken name; MsToken* params; int param_count; MsStmt** body; int body_count; MsString* docstring; } function;
        struct { struct MsExpr* condition; MsStmt* then_branch; MsStmt* else_branch; } if_stmt;
        struct { MsImportItem* items; int item_count; MsToken module_path; } import;
        struct { MsToken keyword; struct MsExpr* value; } return_stmt;
        struct { MsToken name; struct MsExpr* initializer; } var;
        struct { struct MsExpr* condition; MsStmt* body; } while_stmt;
        struct { MsToken var_name; struct MsExpr* iterator; struct MsExpr* collection; MsStmt* body; } for_stmt;
        MsToken break_stmt;
        MsToken continue_stmt;
    };
};
```

Add decl: `MsStmt* ms_stmt_create(MsStmtType type);`

Add to `src/ast.c`:

```c
MsStmt* ms_stmt_create(MsStmtType type) {
    MsStmt* stmt = MS_ALLOCATE(MsStmt, 1);
    memset(stmt, 0, sizeof(MsStmt));
    stmt->type = type;
    return stmt;
}
```

Initial `ms_stmt_free` (EXPRESSION + VAR only):

```c
void ms_stmt_free(MsStmt* stmt) {
    if (stmt == NULL) return;
    switch (stmt->type) {
        case MS_STMT_EXPRESSION: ms_expr_free(stmt->expression); break;
        case MS_STMT_VAR: ms_expr_free(stmt->var.initializer); break;
        default: break;
    }
    MS_FREE(MsStmt, stmt, 1);
}
```

**Verify GREEN**: all 3 tests pass.

**REFACTOR**: none

---

### Cycle 4: All Stmt Types (Block, Function, Class, If, While, For, Import)

**RED** → `test_stmt_types`:

```c
static void test_stmt_types(void) {
    MsStmt* block = ms_stmt_create(MS_STMT_BLOCK);
    block->block.stmts = MS_ALLOCATE(MsStmt*, 3);
    block->block.count = 3;
    for (int i = 0; i < 3; i++) {
        block->block.stmts[i] = ms_stmt_create(MS_STMT_EXPRESSION);
        MsExpr* lit = ms_expr_create(MS_EXPR_LITERAL);
        lit->literal.type = MS_LITERAL_NUMBER;
        lit->literal.value.number = (double)i;
        block->block.stmts[i]->expression = lit;
    }
    assert(block->type == MS_STMT_BLOCK);

    MsStmt* func = ms_stmt_create(MS_STMT_FUNCTION);
    func->function.param_count = 2;
    func->function.params = MS_ALLOCATE(MsToken, 2);
    func->function.body_count = 1;
    func->function.body = MS_ALLOCATE(MsStmt*, 1);
    func->function.body[0] = ms_stmt_create(MS_STMT_EXPRESSION);
    assert(func->type == MS_STMT_FUNCTION);

    MsStmt* if_stmt = ms_stmt_create(MS_STMT_IF);
    assert(if_stmt->type == MS_STMT_IF);
    MsStmt* while_stmt = ms_stmt_create(MS_STMT_WHILE);
    assert(while_stmt->type == MS_STMT_WHILE);
    MsStmt* import = ms_stmt_create(MS_STMT_IMPORT);
    assert(import->type == MS_STMT_IMPORT);

    ms_stmt_free(block);
    ms_stmt_free(func);
    ms_stmt_free(if_stmt);
    ms_stmt_free(while_stmt);
    ms_stmt_free(import);
    printf("  test_stmt_types PASSED\n");
}
```

**GREEN** → full `ms_stmt_free`:

```c
void ms_stmt_free(MsStmt* stmt) {
    if (stmt == NULL) return;
    switch (stmt->type) {
        case MS_STMT_BLOCK:
            if (stmt->block.stmts) {
                for (int i = 0; i < stmt->block.count; i++) ms_stmt_free(stmt->block.stmts[i]);
                MS_FREE(MsStmt*, stmt->block.stmts, stmt->block.count);
            }
            break;
        case MS_STMT_CLASS:
            ms_expr_free(stmt->class_stmt.superclass);
            if (stmt->class_stmt.methods) {
                for (int i = 0; i < stmt->class_stmt.method_count; i++)
                    ms_stmt_free(stmt->class_stmt.methods[i]);
                MS_FREE(MsStmt*, stmt->class_stmt.methods, stmt->class_stmt.method_count);
            }
            break;
        case MS_STMT_EXPRESSION: ms_expr_free(stmt->expression); break;
        case MS_STMT_FUNCTION:
            if (stmt->function.params) MS_FREE(MsToken, stmt->function.params, stmt->function.param_count);
            if (stmt->function.body) {
                for (int i = 0; i < stmt->function.body_count; i++) ms_stmt_free(stmt->function.body[i]);
                MS_FREE(MsStmt*, stmt->function.body, stmt->function.body_count);
            }
            break;
        case MS_STMT_IF:
            ms_expr_free(stmt->if_stmt.condition);
            ms_stmt_free(stmt->if_stmt.then_branch);
            ms_stmt_free(stmt->if_stmt.else_branch);
            break;
        case MS_STMT_IMPORT:
            if (stmt->import.items) MS_FREE(MsImportItem, stmt->import.items, stmt->import.item_count);
            break;
        case MS_STMT_RETURN: ms_expr_free(stmt->return_stmt.value); break;
        case MS_STMT_VAR: ms_expr_free(stmt->var.initializer); break;
        case MS_STMT_WHILE:
            ms_expr_free(stmt->while_stmt.condition);
            ms_stmt_free(stmt->while_stmt.body);
            break;
        case MS_STMT_FOR:
            ms_expr_free(stmt->for_stmt.iterator);
            ms_expr_free(stmt->for_stmt.collection);
            ms_stmt_free(stmt->for_stmt.body);
            break;
        case MS_STMT_BREAK: break;
        case MS_STMT_CONTINUE: break;
    }
    MS_FREE(MsStmt, stmt, 1);
}
```

**Verify GREEN**: all 4 tests pass.

**REFACTOR**: run w/ ASAN → no leaks.

---

### Cycle 5: Nested AST Tree Free

**RED** → `test_nested_ast`:

```c
static void test_nested_ast(void) {
    MsExpr* lit1 = ms_expr_create(MS_EXPR_LITERAL);
    lit1->literal.type = MS_LITERAL_NUMBER;
    lit1->literal.value.number = 1.0;
    MsExpr* lit2 = ms_expr_create(MS_EXPR_LITERAL);
    lit2->literal.type = MS_LITERAL_NUMBER;
    lit2->literal.value.number = 2.0;
    MsExpr* lit3 = ms_expr_create(MS_EXPR_LITERAL);
    lit3->literal.type = MS_LITERAL_NUMBER;
    lit3->literal.value.number = 3.0;
    MsExpr* mul = ms_expr_create(MS_EXPR_BINARY);
    mul->binary.left = lit2;
    mul->binary.right = lit3;
    MsExpr* add = ms_expr_create(MS_EXPR_BINARY);
    add->binary.left = lit1;
    add->binary.right = mul;

    MsStmt* stmt = ms_stmt_create(MS_STMT_EXPRESSION);
    stmt->expression = add;
    ms_stmt_free(stmt);
    printf("  test_nested_ast PASSED\n");
}
```

**Verify GREEN**: all 5 tests pass. Verification cycle for recursive free.

**REFACTOR**: none

---

### Cycle 6: Stmt List Free

**RED** → `test_stmt_list_free`:

```c
static void test_stmt_list_free(void) {
    ms_stmt_list_free(NULL, 0);
    MsStmt** stmts = MS_ALLOCATE(MsStmt*, 3);
    for (int i = 0; i < 3; i++) {
        stmts[i] = ms_stmt_create(MS_STMT_EXPRESSION);
        MsExpr* lit = ms_expr_create(MS_EXPR_LITERAL);
        lit->literal.type = MS_LITERAL_NUMBER;
        lit->literal.value.number = (double)i;
        stmts[i]->expression = lit;
    }
    ms_stmt_list_free(stmts, 3);
    printf("  test_stmt_list_free PASSED\n");
}
```

**Verify RED**: `ms_stmt_list_free` declared but not defined.

**GREEN** → add to `src/ast.c`:

```c
void ms_stmt_list_free(MsStmt** stmts, int count) {
    if (stmts == NULL) return;
    for (int i = 0; i < count; i++) ms_stmt_free(stmts[i]);
    MS_FREE(MsStmt*, stmts, count);
}
```

**Verify GREEN**: all 6 tests pass.

**REFACTOR**: ASAN → complete cleanup.

## Acceptance

- [x] `ms_expr_create(MS_EXPR_LITERAL)` → non-NULL, correct type
- [x] `ms_stmt_create(MS_STMT_VAR)` → non-NULL, correct type
- [x] `ms_expr_free()` on literal → no crash
- [x] `ms_stmt_free()` on block w/ 3 children → all freed recursively
- [x] `ms_stmt_list_free()` handles NULL + count=0
- [x] Nested binary expr tree → complete free, no leak
- [x] All expr + stmt types creatable

## Notes

- Child arrays (`call.args`, `function.body`) → `MS_ALLOCATE` / `MS_FREE`.
- `MsLiteralValue.string_view` → pointer into source (start + length), not owned. No free needed.
- `MsStmt.function.docstring` → `MsString*` from object system, not freed by `ms_stmt_free` (owned by intern table).
- Tagged unions for polymorphism, consistent w/ VM design.
