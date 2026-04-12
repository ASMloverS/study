# Task 09: Scanner — String Interpolation and Block Comments

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Extend scanner: string interpolation `"Hello, ${expr}!"` + nestable block comments `/* ... */`.
**Deps:** T08
**Produces:** Complete lexer w/ interpolation + block comment support

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/scanner.c` | Add string interpolation + block comments |
| Modify | `include/ms/scanner.h` | Optional: interpolation state stack |
| Create | `tests/unit/test_scanner_interp.c` | Interpolation lexer tests |

## Key Data Structures / API

No new public API; internal behavior only.

## Impl Notes

### String Interpolation

Token sequence for `"hello ${name}!"`:
```
MS_TK_STRING_INTERP  "hello "    (prefix, excluding ${)
MS_TK_IDENTIFIER     name
MS_TK_STRING_INTERP_END "!"     (suffix)
```

Track in `MsScanner`:

```c
#define MS_MAX_INTERP_DEPTH 8
typedef struct {
    // ... existing fields ...
    int interp_depth;  // current nesting depth of ${}
} MsScanner;
```

When scanning string:
1. Scan to `${` → emit `MS_TK_STRING_INTERP` (text before `${`), `interp_depth++`
2. Scan tokens normally until `}` closes interpolation brace
3. Matching `}` → `interp_depth--`, resume scanning string remainder
4. Another `${` → repeat step 1
5. `"` → emit `MS_TK_STRING` or `MS_TK_STRING_INTERP_END`

Compiler side (T10): compiles `"a ${x} b"` → `"a " + str(x) + " b"` bytecode.

### Nested Block Comments

```c
// In scanner.c when handling '/':
if (peek == '*') {
    int depth = 1;
    advance(); advance();  // skip /*
    while (depth > 0 && !is_at_end()) {
        if (c == '/' && peek == '*') { depth++; advance(); }
        else if (c == '*' && peek == '/') { depth--; advance(); }
        if (c == '\n') { line++; column = 1; }
        advance();
    }
}
```

### String Escape Sequences

All escapes from T08:
- `\n` → newline, `\t` → tab, `\r` → carriage return
- `\\` → backslash, `\"` → double quote
- `\0` → null byte
- `\x41` → hex escape (optional)

## C Unit Tests

```c
// tests/unit/test_scanner_interp.c
#include "test_assert.h"
#include "ms/scanner.h"

static void test_simple_interp(void) {
    MsScanner s;
    ms_scanner_init(&s, "\"hello ${name}!\"");
    MsToken t1 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t1.type, MS_TK_STRING_INTERP);
    // t1 contains "hello " prefix

    MsToken t2 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t2.type, MS_TK_IDENTIFIER);
    // t2 is "name"

    MsToken t3 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t3.type, MS_TK_STRING_INTERP_END);
    // t3 contains "!" suffix
}

static void test_nested_interp(void) {
    MsScanner s;
    ms_scanner_init(&s, "\"${1 + 2} items\"");
    MsToken t1 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t1.type, MS_TK_STRING_INTERP);
    // empty prefix ""

    MsToken t2 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t2.type, MS_TK_NUMBER_INT);
    MsToken t3 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t3.type, MS_TK_PLUS);
    MsToken t4 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t4.type, MS_TK_NUMBER_INT);

    MsToken t5 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t5.type, MS_TK_STRING_INTERP_END);
    // " items" suffix
}

static void test_block_comment(void) {
    MsScanner s;
    ms_scanner_init(&s, "a /* comment */ b");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_EOF_TOKEN);
}

static void test_nested_block_comment(void) {
    MsScanner s;
    ms_scanner_init(&s, "a /* outer /* inner */ still comment */ b");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_EOF_TOKEN);
}

int main(void) {
    test_simple_interp();
    test_nested_interp();
    test_block_comment();
    test_nested_block_comment();
    printf("test_scanner_interp: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/string_interpolation.ms (run after T13 + T23)
var name = "world"
print("hello ${name}!")
// expect: hello world!
print("${1 + 2} items")
// expect: 3 items
print("a ${"b"} c")
// expect: a b c
var x = 10
print("x is ${x}")
// expect: x is 10

// Block comments should be ignored
/* this is a comment */
print("after comment")
// expect: after comment
/* nested /* comments */ are fine */
print("ok")
// expect: ok
```
