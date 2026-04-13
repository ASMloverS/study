# T13: Parser

**Phase**: 4 · **Deps**: T10 (Scanner), T11 (AST Nodes) · **Complexity**: High

## Goal

Token stream → AST. Pratt parser for expressions (operator precedence), recursive descent for statements.

## Files

| File | Purpose |
|------|---------|
| `src/parser.h` | `MsParser` struct, API |
| `src/parser.c` | Full parser impl |

## TDD Cycles

### Cycle 1: Parser Init + Literal Expressions

**RED**: `ms_parser_init`, `ms_parser_parse`, `ms_parser_had_error` undefined → link error.

- `test_parser_init()`: `ms_parser_init(&parser, "")` → `hadError == false`
- `test_number_literal()`: parse `"42"` → 1 stmt, `MS_EXPR_LITERAL` value 42
- `test_string_literal()`: parse `"\"hello\""` → string literal node
- `test_bool_nil_literals()`: parse `"true"`, `"false"`, `"nil"` → correct literal types
- `test_grouping()`: parse `"(1 + 2)"` → `MS_EXPR_GROUPING` wrapping inner expr

**GREEN**: Create `src/parser.h`:
```c
#ifndef MS_PARSER_H
#define MS_PARSER_H

#include "scanner.h"
#include "ast.h"

typedef struct {
    MsToken token;
    char message[256];
} MsParseError;

typedef struct {
    MsScanner scanner;
    MsToken current;
    MsToken previous;
    bool hadError;
    bool panicMode;
    MsParseError lastError;
} MsParser;

void ms_parser_init(MsParser* parser, const char* source);
int ms_parser_parse(MsParser* parser, MsStmt*** outStatements);
bool ms_parser_had_error(const MsParser* parser);

#endif
```

Create `src/parser.c`:
- `ms_parser_init()`: init scanner, `hadError = false`, `panicMode = false`
- `ms_parser_parse()`: stub returning 0 statements for empty input
- Internal: `advance()`, `consume()`, `check()`, `match()` helpers
- `parsePrimary()`: handle `NUMBER`, `STRING`, `TRUE`, `FALSE`, `NIL`, `LEFT_PAREN` (grouping)
- Wrap each expression in `MS_STMT_EXPR`
- `parseExpr()` → `parsePrimary()` only (no binary/unary yet)

**Verify GREEN**: `cmake --build build && ./build/test_parser`

**REFACTOR**: Extract `allocStmt()` + `allocExpr()` helpers.

### Cycle 2: Unary + Binary Arithmetic (Precedence)

**RED**: Binary/unary operators not recognized → wrong AST shape.

- `test_unary_negate()`: parse `"-42"` → `MS_EXPR_UNARY` with `MS_TOKEN_MINUS`
- `test_unary_not()`: parse `"!true"` → `MS_EXPR_UNARY` with `MS_TOKEN_BANG`
- `test_binary_add()`: parse `"1 + 2"` → `MS_EXPR_BINARY` with `MS_TOKEN_PLUS`
- `test_precedence()`: parse `"1 + 2 * 3"` → `Binary(PLUS, Literal(1), Binary(STAR, Literal(2), Literal(3)))`
- `test_left_associativity()`: parse `"1 - 2 - 3"` → `(1-2)-3`

**GREEN**: Implement Pratt parser infrastructure:
- Precedence chain: `parseAssignment`→`parseOr`→`parseAnd`→`parseEquality`→`parseComparison`→`parseTerm`→`parseFactor`→`parseUnary`→`parseCall`→`parsePrimary`
- Each level: call next-higher for left operand, loop on infix tokens at this level
- `parseUnary()`: `-`/`!` → consume, recurse, return `MS_EXPR_UNARY`
- `parseTerm()`: left via `parseFactor`, loop `+`/`-` → `MS_EXPR_BINARY`
- `parseFactor()`: left via `parseUnary`, loop `*`/`/`/`%` → `MS_EXPR_BINARY`
- Higher levels delegate to next for now

**Verify GREEN**: build + run → arithmetic/precedence tests pass.

**REFACTOR**: Consolidate binary parsing into generic `parseBinary()` helper parameterized by op set + next-precedence fn.

### Cycle 3: Comparison, Equality, Logical Expressions

**RED**: Comparison/equality/logical operators not handled → assertion errors.

- `test_comparison()`: parse `"1 < 2"`, `"1 <= 2"`, `"1 > 2"`, `"1 >= 2"` → `MS_EXPR_BINARY`
- `test_equality()`: parse `"1 == 2"`, `"1 != 2"` → correct binary nodes
- `test_logical_and()`: parse `"true and false"` → `MS_EXPR_LOGICAL` with `MS_TOKEN_AND`
- `test_logical_or()`: parse `"true or false"` → `MS_EXPR_LOGICAL` with `MS_TOKEN_OR`
- `test_mixed_precedence()`: parse `"1 + 2 < 3 * 4"` → arithmetic > comparison precedence

**GREEN**:
- `parseComparison()`: after `parseTerm`, loop `<`/`<=`/`>`/`>=` → `MS_EXPR_BINARY`
- `parseEquality()`: after `parseComparison`, loop `==`/`!=` → `MS_EXPR_BINARY`
- `parseAnd()`: after `parseEquality`, loop `and` → `MS_EXPR_LOGICAL`
- `parseOr()`: after `parseAnd`, loop `or` → `MS_EXPR_LOGICAL`
- `parseAssignment()` delegates to `parseOr` for now

**Verify GREEN**: build + run → all expression tests pass.

**REFACTOR**: Verify all precedence levels correct + consistent.

### Cycle 4: Variable References + Assignment

**RED**: Identifiers not parsed as variables → assertion errors.

- `test_variable_reference()`: parse `"x"` → `MS_EXPR_VARIABLE` name "x"
- `test_assignment()`: parse `"x = 42"` → `MS_EXPR_ASSIGN` name "x", value literal 42
- `test_compound_expression()`: parse `"x + y"` → binary with two variable refs

**GREEN**:
- `parsePrimary()`: handle `MS_TOKEN_IDENTIFIER` → `MS_EXPR_VARIABLE`
- `parseAssignment()`: after `parseOr`, check if result is `MS_EXPR_VARIABLE` + next is `=`; if so, consume `=`, parse value, return `MS_EXPR_ASSIGN`; else return as-is
- `parseCall()` level between `parseUnary` and `parsePrimary`: delegate to `parsePrimary` for now

**Verify GREEN**: build + run → variable/assignment tests pass.

**REFACTOR**: Ensure assignment right-associative (`a = b = c` → `a = (b = c)`).

### Cycle 5: Variable Declarations + Block Statements

**RED**: `var` keyword and blocks not handled → assertion errors.

- `test_var_decl_no_init()`: parse `"var x"` → `MS_STMT_VAR_DECL`, no initializer
- `test_var_decl_with_init()`: parse `"var x = 42"` → `MS_STMT_VAR_DECL` with initializer
- `test_block()`: parse `"{ var x = 1\n var y = 2 }"` → `MS_STMT_BLOCK` with two var decls
- `test_empty_block()`: parse `"{}"` → block with zero statements

**GREEN**:
- `parseDeclaration()`: `MS_TOKEN_VAR` → `parseVarDeclaration()`; else → `parseStatement()`
- `parseVarDeclaration()`: consume `var`, identifier, optional `=` + expr, newline/semicolon → `MS_STMT_VAR_DECL`
- `parseBlock()`: consume `{`, loop stmts until `}`, consume `}` → `MS_STMT_BLOCK`
- `parseStatement()`: `{` → block; else → `parseExpressionStatement()`
- `parseExpressionStatement()`: parse expr, consume newline/semicolon → `MS_STMT_EXPR`
- `ms_parser_parse()`: loop `parseDeclaration()` until EOF, collect into dynamic array

**Verify GREEN**: build + run → declaration/block tests pass.

**REFACTOR**: None.

### Cycle 6: Control Flow Statements

**RED**: Control flow keywords not handled → assertion errors.

- `test_if_statement()`: parse `"if (true) print 1"` → `MS_STMT_IF`
- `test_if_else()`: parse `"if (true) print 1 else print 2"` → `MS_STMT_IF` with else
- `test_while_statement()`: parse `"while (i < 10) print i"` → `MS_STMT_WHILE`
- `test_for_statement()`: parse `"for (var i = 0; i < 10; i = i + 1) print i"` → `MS_STMT_FOR`
- `test_return_statement()`: parse `"return 42"` → `MS_STMT_RETURN`
- `test_break_continue()`: parse `"break"`, `"continue"` → `MS_STMT_BREAK` / `MS_STMT_CONTINUE`

**GREEN**: Extend `parseStatement()` dispatch:
- `MS_TOKEN_IF` → consume `if`, `(`, expr, `)`, stmt, optional `else` + stmt → `MS_STMT_IF`
- `MS_TOKEN_WHILE` → consume `while`, `(`, expr, `)`, stmt → `MS_STMT_WHILE`
- `MS_TOKEN_FOR` → consume `for`, `(`, init (var decl or expr), `;`, condition, `;`, increment, `)`, body → `MS_STMT_FOR`
- `MS_TOKEN_RETURN` → consume `return`, optional expr, newline → `MS_STMT_RETURN`
- `MS_TOKEN_BREAK` → consume → `MS_STMT_BREAK`
- `MS_TOKEN_CONTINUE` → consume → `MS_STMT_CONTINUE`

**Verify GREEN**: build + run → control flow tests pass.

**REFACTOR**: Extract "parse parenthesized expression" helper.

### Cycle 7: Function Declarations

**RED**: `fn` keyword not handled → assertion errors.

- `test_function_decl()`: parse `"fn add(a, b) { return a + b }"` → `MS_STMT_FUNC_DECL` name "add", params ["a","b"]
- `test_function_no_params()`: parse `"fn foo() { print 1 }"` → zero params
- `test_function_empty_body()`: parse `"fn noop() {}"` → empty body

**GREEN**:
- `parseDeclaration()`: `MS_TOKEN_FN` → `parseFuncDeclaration()`
- `parseFuncDeclaration()`: consume `fn`, identifier (name), `(`, comma-separated identifiers (params), `)`, block body → `MS_STMT_FUNC_DECL`
- `parsePrimary()` / `parseCall()`: after primary, if `(` → comma-separated args → `MS_EXPR_CALL`

**Verify GREEN**: build + run → function tests pass.

**REFACTOR**: Share param-list parsing with call arg-list parsing.

### Cycle 8: Class Declarations + Import Statements

**RED**: class/import/this/super not handled → assertion errors.

- `test_class_decl()`: parse `"class Foo { fn bar() { print 1 } }"` → `MS_STMT_CLASS_DECL` name "Foo", 1 method
- `test_class_inheritance()`: parse `"class Child < Parent { }"` → superclass "Parent"
- `test_import_simple()`: parse `"import math"` → `MS_STMT_IMPORT` module "math"
- `test_import_from()`: parse `"from math import sqrt"` → `MS_STMT_IMPORT` module "math", items ["sqrt"]
- `test_this_super()`: parse `"this.x"`, `"super.greet()"` → `MS_EXPR_GET` on this/super node

**GREEN**:
- `parseDeclaration()`: `MS_TOKEN_CLASS` → `parseClassDeclaration()`
- `parseClassDeclaration()`: consume `class`, name, optional `<` + superclass, `{`, loop methods, `}` → `MS_STMT_CLASS_DECL`
- `parseDeclaration()`: `MS_TOKEN_IMPORT` → `parseImportStatement()`
- `parseImportStatement()`: `import` module; or `from` module `import` items → `MS_STMT_IMPORT`
- `parsePrimary()`: `MS_TOKEN_THIS` → `MS_EXPR_THIS`; `MS_TOKEN_SUPER` → `MS_EXPR_SUPER`
- `parseCall()`: `.name` (get), `.name = expr` (set), `[index]` (subscript), `[index] = expr` (subscript assign)
- List literal: `[elem1, elem2]` → `MS_EXPR_LIST`

**Verify GREEN**: build + run → all class/import/get/set tests pass.

**REFACTOR**: Consolidate property access parsing.

### Cycle 9: Error Recovery

**RED**: Error recovery not functional → parser crashes on invalid input.

- `test_error_missing_semicolon()`: parse `"var x = 42 var y = 1"` → error reported, both decls still parsed
- `test_error_unclosed_brace()`: parse `"{ var x = 1"` → error reported
- `test_error_invalid_token()`: parse `"var 42 = x"` → error with line/column
- `test_error_multiple()`: parse `"var x =\nvar y = 1 + \nprint 1"` → multiple errors, no crash
- `test_had_error_flag()`: `ms_parser_had_error()` → true after any error

**GREEN**:
- `synchronize()`: skip tokens until statement boundary (`NEWLINE`, `RIGHT_BRACE`, `CLASS`, `FN`, `VAR`, `FOR`, `IF`, `WHILE`, `PRINT`, `RETURN`)
- `parseDeclaration()` / `parseStatement()`: on error → `synchronize()` before continuing
- `consume()`: mismatch → `hadError = true`, store in `lastError`, enter `panicMode`, call `synchronize()`
- `ms_parser_parse()`: continues looping after errors, returns all successfully parsed stmts

**Verify GREEN**: build + run → error recovery tests pass, no crash.

**REFACTOR**: Add line/column to all error messages.

## Acceptance Criteria

- [x] Parse `"var x = 42"` → VarDecl node
- [x] Parse `"1 + 2 * 3"` → `Binary(PLUS, Literal(1), Binary(STAR, Literal(2), Literal(3)))`
- [x] Parse `"if (true) print 1 else print 2"` → If with then/else
- [x] Parse `"fn add(a, b) { return a + b }"` → Function node
- [x] Parse `"class Foo { fn bar() { ... } }"` → Class node
- [x] Parse `"import math"` / `"from math import sqrt"` → Import nodes
- [x] Error recovery: invalid syntax → no crash, error reported
- [x] `ms_parser_had_error()` → true after invalid input
- [x] Complex programs parse correctly

## Notes

- Pratt (TDOP) for expressions, recursive descent for statements. Matches clox architecture.
- `ms_parser_parse()` → count of parsed stmts (-1 on fatal error). Caller frees via `ms_stmt_list_free()`.
- Expression types: literals, variables, unary, binary, logical, grouping, assignment, call, get/set, this/super, list, subscript.
- Statement types: var/fn/class/import decls, block, if, while, for, return, break, continue, expression.
