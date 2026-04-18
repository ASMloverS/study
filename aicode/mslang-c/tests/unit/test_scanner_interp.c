#include "../../tests/test_assert.h"
#include "ms/scanner.h"
#include <stdio.h>
#include <string.h>

static void test_simple_interp(void) {
    MsScanner s;
    ms_scanner_init(&s, "\"hello ${name}!\"");
    MsToken t1 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t1.type, MS_TK_STRING_INTERP);

    MsToken t2 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t2.type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(t2.length, 4);

    MsToken t3 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t3.type, MS_TK_STRING_INTERP_END);

    MsToken t4 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t4.type, MS_TK_EOF_TOKEN);
}

static void test_interp_empty_prefix(void) {
    MsScanner s;
    ms_scanner_init(&s, "\"${1 + 2} items\"");
    MsToken t1 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t1.type, MS_TK_STRING_INTERP);

    MsToken t2 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t2.type, MS_TK_NUMBER_INT);
    MsToken t3 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t3.type, MS_TK_PLUS);
    MsToken t4 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t4.type, MS_TK_NUMBER_INT);

    MsToken t5 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t5.type, MS_TK_STRING_INTERP_END);

    MsToken t6 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t6.type, MS_TK_EOF_TOKEN);
}

static void test_interp_middle_segment(void) {
    /* "a ${x} b ${y} c" produces: INTERP IDENT INTERP IDENT INTERP_END */
    MsScanner s;
    ms_scanner_init(&s, "\"a ${x} b ${y} c\"");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STRING_INTERP);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STRING_INTERP);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STRING_INTERP_END);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_EOF_TOKEN);
}

static void test_plain_string_unchanged(void) {
    MsScanner s;
    ms_scanner_init(&s, "\"hello world\"");
    MsToken t = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t.type, MS_TK_STRING);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_EOF_TOKEN);
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

static void test_block_comment_newline_tracking(void) {
    MsScanner s;
    ms_scanner_init(&s, "/* line1\nline2 */ x");
    MsToken t = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t.type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(t.line, 2);
}

static void test_interp_asi_after_end(void) {
    /* STRING_INTERP_END triggers ASI like STRING */
    MsScanner s;
    ms_scanner_init(&s, "\"${x}\"\ny");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STRING_INTERP);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STRING_INTERP_END);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NEWLINE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
}

int main(void) {
    test_simple_interp();
    test_interp_empty_prefix();
    test_interp_middle_segment();
    test_plain_string_unchanged();
    test_block_comment();
    test_nested_block_comment();
    test_block_comment_newline_tracking();
    test_interp_asi_after_end();
    printf("test_scanner_interp: all passed\n");
    return 0;
}
