# Task 08: Scanner — Basic Tokenization

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Scanner producing all basic tokens: numbers (int/float), identifiers, keywords, operators, string literals.
**Deps:** T01
**Produces:** `MsScanner` converts source → token stream; basic ASI support

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/token.h` | `TokenType` enum (X-macro), `Token` struct |
| Create | `include/ms/scanner.h` | `MsScanner` state + API |
| Create | `src/scanner.c` | Lexer impl |
| Create | `tests/unit/test_scanner.c` | Basic lexer tests |

## Key Data Structures / API

```c
// include/ms/token.h
#define MS_TOKEN_TYPES(X) \
    /* Single-char */ \
    X(LEFT_PAREN) X(RIGHT_PAREN) X(LEFT_BRACE) X(RIGHT_BRACE) \
    X(LEFT_BRACKET) X(RIGHT_BRACKET) \
    X(COMMA) X(DOT) X(SEMICOLON) X(COLON) X(QUESTION) \
    /* One or two char */ \
    X(BANG) X(BANG_EQUAL) \
    X(EQUAL) X(EQUAL_EQUAL) \
    X(GREATER) X(GREATER_EQUAL) X(GREATER_GREATER) \
    X(LESS) X(LESS_EQUAL) X(LESS_LESS) \
    X(PLUS) X(PLUS_EQUAL) \
    X(MINUS) X(MINUS_EQUAL) \
    X(STAR) X(STAR_EQUAL) \
    X(SLASH) X(SLASH_EQUAL) \
    X(PERCENT) X(PERCENT_EQUAL) \
    X(AMPERSAND) X(PIPE) X(CARET) X(TILDE) \
    X(DOT_DOT) \
    /* Literals */ \
    X(IDENTIFIER) X(STRING) X(STRING_INTERP) X(STRING_INTERP_END) \
    X(NUMBER_INT) X(NUMBER_FLOAT) \
    /* Keywords */ \
    X(AND) X(OR) X(NOT_KW) \
    X(IF) X(ELSE) X(WHILE) X(FOR) X(IN) X(BREAK) X(CONTINUE) X(RETURN) \
    X(VAR) X(FUN) X(CLASS) X(THIS) X(SUPER) X(STATIC) \
    X(TRUE) X(FALSE) X(NIL) \
    X(PRINT) X(IMPORT) X(FROM) X(AS) \
    X(TRY) X(CATCH) X(THROW) X(DEFER) X(YIELD) \
    X(SWITCH) X(CASE) X(DEFAULT) X(ENUM) \
    /* Virtual / special */ \
    X(NEWLINE) \
    X(ERROR) X(EOF_TOKEN)

typedef enum {
#define X(name) MS_TK_##name,
    MS_TOKEN_TYPES(X)
#undef X
    MS_TK_COUNT
} MsTokenType;

typedef struct {
    MsTokenType type;
    const char* start;  // points into source buffer
    int length;
    int line;
    int column;
} MsToken;

const char* ms_token_type_name(MsTokenType type);
```

```c
// include/ms/scanner.h
typedef struct {
    const char* source;
    const char* start;    // start of current token
    const char* current;  // current read position
    int line;
    int column;
    int paren_depth;      // ( depth — suppresses ASI inside parens
    int bracket_depth;    // [ depth — suppresses ASI
    int interp_depth;     // string interpolation nesting
    MsTokenType prev_type; // for ASI determination
} MsScanner;

void    ms_scanner_init(MsScanner* s, const char* source);
MsToken ms_scanner_next(MsScanner* s);

typedef struct {
    const char* current;
    int line;
    int column;
    int paren_depth;
    int bracket_depth;
    MsTokenType prev_type;
} MsScannerState;

MsScannerState ms_scanner_save(const MsScanner* s);
void           ms_scanner_restore(MsScanner* s, MsScannerState st);
```

## Impl Notes

- **Keywords**: trie or length+first-char dispatch. Full list: `and or not if else while for in break continue return var fun class this super static true false nil print import from as try catch throw defer yield switch case default enum`
- **Numbers**: scan digits; `.` → float; prefixes: `0x` hex, `0b` binary, `0o` octal
- **Strings**: `"..."` w/ escapes `\n \t \r \\ \" \0`; `${...}` interp → T09
- **ASI**: at newline, emit `MS_TK_NEWLINE` if `paren_depth==0 && bracket_depth==0` and `prev_type` ∈:
  - `IDENTIFIER NUMBER_INT NUMBER_FLOAT STRING STRING_INTERP_END`
  - `RIGHT_PAREN RIGHT_BRACKET RIGHT_BRACE`
  - `TRUE FALSE NIL RETURN BREAK CONTINUE`
- **Line comments**: `//` to EOL
- **Block comments**: nestable `/* ... */` → T09

## C Unit Tests

```c
// tests/unit/test_scanner.c
#include "test_assert.h"
#include "ms/scanner.h"

static void test_arithmetic_tokens(void) {
    MsScanner s;
    ms_scanner_init(&s, "1 + 2.5");
    MsToken t1 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t1.type, MS_TK_NUMBER_INT);
    TEST_ASSERT_EQ(t1.length, 1);
    MsToken t2 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t2.type, MS_TK_PLUS);
    MsToken t3 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t3.type, MS_TK_NUMBER_FLOAT);
    MsToken t4 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t4.type, MS_TK_EOF_TOKEN);
}

static void test_keywords(void) {
    MsScanner s;
    ms_scanner_init(&s, "var fun class if else while for return");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_VAR);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_FUN);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_CLASS);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IF);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_ELSE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_WHILE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_FOR);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RETURN);
}

static void test_asi(void) {
    MsScanner s;
    ms_scanner_init(&s, "return\nx");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RETURN);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NEWLINE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
}

static void test_asi_suppressed_in_parens(void) {
    MsScanner s;
    ms_scanner_init(&s, "foo(\n1\n)");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LEFT_PAREN);
    // No NEWLINE inside parens
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NUMBER_INT);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RIGHT_PAREN);
}

static void test_string_escapes(void) {
    MsScanner s;
    ms_scanner_init(&s, "\"hello\\nworld\"");
    MsToken t = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t.type, MS_TK_STRING);
    // t.start points to opening quote, t.length includes quotes
}

int main(void) {
    test_arithmetic_tokens();
    test_keywords();
    test_asi();
    test_asi_suppressed_in_parens();
    test_string_escapes();
    printf("test_scanner: all passed\n");
    return 0;
}
```

## .ms Integration Tests

Tested indirectly via compiler + VM:

```ms
// tests/fixtures/scanner_asi.ms (run after T13)
// ASI: semicolons should be auto-inserted at newlines
var a = 1
var b = 2
print(a + b)
// expect: 3

// ASI suppressed inside parens
print(
  a +
  b
)
// expect: 3
```
