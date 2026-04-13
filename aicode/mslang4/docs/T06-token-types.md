# T06: Token Types

**Phase**: 1 — Foundation
**Deps**: T02 (Common Definitions)
**Complexity**: Low

## Goal

Complete token type enum + token struct. Header-only module (no `.c`). Used by scanner, parser, compiler.

## Files

| File | Purpose |
|------|---------|
| `src/token.h` | `MsTokenType` enum + `MsToken` struct |
| `tests/unit/test_token.c` | Unit tests |

## TDD Cycles

### Cycle 1: `MsTokenType` Enum

**RED** — Create `tests/unit/test_token.c`:

```c
#include "token.h"
#include <stdio.h>
#include <stdlib.h>

static void test_token_count(void) {
    if (MS_TOKEN_EOF + 1 != 49) {
        fprintf(stderr, "FAIL: expected 49 token types, got %d\n", MS_TOKEN_EOF + 1);
        exit(1);
    }
}

int main(void) {
    test_token_count();
    printf("test_token_count passed\n");
    return 0;
}
```

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_token tests/unit/test_token.c)
target_include_directories(test_token PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_token COMMAND test_token)
```

`token.h` doesn't exist → compile error.

**Verify RED**: `cmake --build build` → `token.h` not found

**GREEN** — Create `src/token.h`:

```c
#ifndef MS_TOKEN_H
#define MS_TOKEN_H

typedef enum {
    MS_TOKEN_LEFT_PAREN, MS_TOKEN_RIGHT_PAREN,
    MS_TOKEN_LEFT_BRACE, MS_TOKEN_RIGHT_BRACE,
    MS_TOKEN_LEFT_BRACKET, MS_TOKEN_RIGHT_BRACKET,
    MS_TOKEN_COMMA, MS_TOKEN_DOT, MS_TOKEN_NEWLINE, MS_TOKEN_SEMICOLON, MS_TOKEN_COLON,
    MS_TOKEN_BANG, MS_TOKEN_BANG_EQUAL,
    MS_TOKEN_EQUAL, MS_TOKEN_EQUAL_EQUAL,
    MS_TOKEN_GREATER, MS_TOKEN_GREATER_EQUAL,
    MS_TOKEN_LESS, MS_TOKEN_LESS_EQUAL,
    MS_TOKEN_PLUS, MS_TOKEN_MINUS, MS_TOKEN_STAR, MS_TOKEN_SLASH, MS_TOKEN_PERCENT,
    MS_TOKEN_IDENTIFIER, MS_TOKEN_STRING, MS_TOKEN_NUMBER,
    MS_TOKEN_AND, MS_TOKEN_CLASS, MS_TOKEN_ELSE, MS_TOKEN_FALSE,
    MS_TOKEN_FN, MS_TOKEN_FOR, MS_TOKEN_IF, MS_TOKEN_NIL,
    MS_TOKEN_OR, MS_TOKEN_PRINT, MS_TOKEN_RETURN, MS_TOKEN_SUPER,
    MS_TOKEN_THIS, MS_TOKEN_TRUE, MS_TOKEN_VAR, MS_TOKEN_WHILE,
    MS_TOKEN_BREAK, MS_TOKEN_CONTINUE,
    MS_TOKEN_IMPORT, MS_TOKEN_FROM, MS_TOKEN_AS,
    MS_TOKEN_ERROR, MS_TOKEN_EOF
} MsTokenType;

#endif
```

Ordering: single-char → compound ops → literals → keywords → special (`ERROR`, `EOF`). Values sequential from 0. `MS_TOKEN_ERROR` carries error msg as lexeme. `MS_TOKEN_EOF` marks end of input.

**Verify GREEN**: `cmake --build build && ctest --test-dir build -R test_token`

**REFACTOR**: None needed.

### Cycle 2: `MsToken` Struct

**RED** — Add test:

```c
static void test_token_struct(void) {
    MsToken tok;
    tok.type = MS_TOKEN_NUMBER;
    tok.start = "42";
    tok.length = 2;
    tok.line = 1;
    tok.column = 5;

    if (tok.type != MS_TOKEN_NUMBER) {
        fprintf(stderr, "FAIL: type field not set correctly\n");
        exit(1);
    }
    if (tok.start[0] != '4' || tok.start[1] != '2') {
        fprintf(stderr, "FAIL: start field not set correctly\n");
        exit(1);
    }
    if (tok.length != 2) {
        fprintf(stderr, "FAIL: length field not set correctly\n");
        exit(1);
    }
    if (tok.line != 1) {
        fprintf(stderr, "FAIL: line field not set correctly\n");
        exit(1);
    }
    if (tok.column != 5) {
        fprintf(stderr, "FAIL: column field not set correctly\n");
        exit(1);
    }
}
```

`MsToken` undefined → compile error.

**Verify RED**: `cmake --build build` → `MsToken` undeclared

**GREEN** — Add to `src/token.h`:

```c
typedef struct {
    MsTokenType type;
    const char* start;
    int length;
    int line;
    int column;
} MsToken;
```

`start` = non-owning ptr into source buffer (no alloc). `line`/`column` for error reporting.

**Verify GREEN**: `cmake --build build && ctest --test-dir build -R test_token`

**REFACTOR**: None needed.

### Cycle 3: Enum Ordering + Sizeof Checks

**RED** — Add tests:

```c
static void test_enum_ordering(void) {
    if (MS_TOKEN_LEFT_PAREN != 0) {
        fprintf(stderr, "FAIL: MS_TOKEN_LEFT_PAREN should be 0\n");
        exit(1);
    }
    if (MS_TOKEN_BANG_EQUAL <= MS_TOKEN_BANG) {
        fprintf(stderr, "FAIL: compound operators should come after single-char\n");
        exit(1);
    }
    if (MS_TOKEN_ERROR >= MS_TOKEN_EOF) {
        fprintf(stderr, "FAIL: EOF should be last token\n");
        exit(1);
    }
    if (MS_TOKEN_LEFT_BRACE != MS_TOKEN_RIGHT_PAREN + 1) {
        fprintf(stderr, "FAIL: tokens should be sequential\n");
        exit(1);
    }
}

static void test_token_size(void) {
    if (sizeof(MsToken) > 32) {
        fprintf(stderr, "FAIL: MsToken is unexpectedly large: %zu bytes\n", sizeof(MsToken));
        exit(1);
    }
    if (sizeof(MsToken) < sizeof(MsTokenType) + sizeof(const char*) + sizeof(int) * 3) {
        fprintf(stderr, "FAIL: MsToken is too small, fields may be missing\n");
        exit(1);
    }
}
```

**Verify RED**: `cmake --build build && ctest --test-dir build -R test_token` — already passes (Cycle 1 ordering correct).

**GREEN** — No changes. Cycles 1-2 already satisfy these.

**Verify GREEN**: `cmake --build build && ctest --test-dir build -R test_token`
`sizeof(MsToken)` typically 24 bytes on 64-bit (enum=4 + ptr=8 + int×3=12 + padding).

**REFACTOR**: 24 bytes on 64-bit is reasonable. No packing pragmas needed.

## Acceptance Criteria

- [x] `token.h` compiles from `.c`
- [x] `MsTokenType` has all 49 types
- [x] `MsToken` fields: `type`, `start`, `length`, `line`, `column`
- [x] `sizeof(MsToken)` reasonable (24-32 bytes on 64-bit)
- [x] Enum sequential from 0

## Notes

- Header-only — no `.c`.
- Ordering: single-char → compound → literals → keywords → special (`ERROR`, `EOF`).
- `MsToken.start` non-owning ptr into source. Scanner creates tokens without lexeme alloc.
