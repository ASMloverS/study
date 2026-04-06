# Task 09: Scanner — String Interpolation and Block Comments

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Extend scanner to handle string interpolation `"Hello, ${expr}!"` and nestable block comments `/* ... */`.
**Dependencies:** T08
**Produces:** 完整词法分析器, 支持插值和块注释

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/scanner.c` | 添加字符串插值和块注释 |
| Modify | `include/ms/scanner.h` | 可选: 插值状态栈 |
| Create | `tests/unit/test_scanner_interp.c` | 插值词法测试 |

## Key Data Structures / API

无新公共 API, 仅修改内部行为。

## Implementation Notes

### 字符串插值

当扫描 `"hello ${name}!"` 时产出的 token 序列:
```
MS_TK_STRING_INTERP  "hello "    (前缀, 不含 ${)
MS_TK_IDENTIFIER     name
MS_TK_STRING         "!"         (后缀) 或 MS_TK_STRING_INTERP (若还有更多 ${)
```

实现策略 — 在 MsScanner 中追踪插值状态:

```c
// 在 scanner 结构中添加:
#define MS_MAX_INTERP_DEPTH 8
typedef struct {
    // ... 已有字段 ...
    int interp_depth;  // 当前嵌套的 ${} 层数
} MsScanner;
```

扫描字符串时:
1. 扫描到 `${` → 产出 `MS_TK_STRING_INTERP` (包含到 `${` 前的文本), interp_depth++
2. 正常扫描 tokens 直到遇到 `}` 且 brace_depth 回到插值层的初始值
3. 遇到配对的 `}` → interp_depth--, 继续扫描字符串剩余部分
4. 如果又遇到 `${` → 重复步骤 1
5. 遇到 `"` → 产出最终的 `MS_TK_STRING` 或 `MS_TK_STRING_INTERP_END`

编译器侧 (T10 中实现): 将 `"a ${x} b"` 编译为 `"a " + str(x) + " b"` 的字节码。

### 嵌套块注释

```c
// scanner.c 中处理 '/' 时:
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

### 字符串转义序列完善

在 T08 基础上确保所有转义正确处理:
- `\n` → newline, `\t` → tab, `\r` → carriage return
- `\\` → backslash, `\"` → double quote
- `\0` → null byte
- `\x41` → hex escape (可选)

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
