#include "../../tests/test_assert.h"
#include "ms/scanner.h"
#include <stdio.h>

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

/* MIN-4: save/restore round-trip - restoring before a token re-scans identically. */
static void test_save_restore_plain(void) {
    MsScanner s;
    ms_scanner_init(&s, "foo bar");
    /* advance past first token */
    MsToken first = ms_scanner_next(&s);
    TEST_ASSERT_EQ(first.type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(first.length, 3);

    MsScannerState saved = ms_scanner_save(&s);
    MsToken second = ms_scanner_next(&s);
    TEST_ASSERT_EQ(second.type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(second.length, 3);

    /* restore and re-scan: must produce the same token */
    ms_scanner_restore(&s, saved);
    MsToken replay = ms_scanner_next(&s);
    TEST_ASSERT_EQ(replay.type, second.type);
    TEST_ASSERT_EQ(replay.length, second.length);
    TEST_ASSERT_EQ(replay.line, second.line);
    TEST_ASSERT_EQ(replay.column, second.column);
}

/* MIN-4: save inside an interpolation expression, restore, re-scan same token (interp state preserved). */
static void test_save_restore_inside_interp(void) {
    MsScanner s;
    ms_scanner_init(&s, "\"${abc}\"");
    /* consume STRING_INTERP */
    MsToken interp = ms_scanner_next(&s);
    TEST_ASSERT_EQ(interp.type, MS_TK_STRING_INTERP);

    /* save before the identifier */
    MsScannerState saved = ms_scanner_save(&s);
    MsToken id1 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(id1.type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(id1.length, 3);

    ms_scanner_restore(&s, saved);
    MsToken id2 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(id2.type, id1.type);
    TEST_ASSERT_EQ(id2.length, id1.length);
    TEST_ASSERT_EQ(id2.start, id1.start);
}

/* MIN-1: unterminated block comment produces an error token. */
static void test_unterminated_block_comment(void) {
    MsScanner s;
    ms_scanner_init(&s, "x /* no end");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    MsToken err = ms_scanner_next(&s);
    TEST_ASSERT_EQ(err.type, MS_TK_ERROR);
}

/* MAJ-1: nested braces inside ${} must not close interpolation early. */
static void test_interp_nested_braces(void) {
    /* "a ${f({1:2})} b" - the map literal's {} must not close the interp */
    MsScanner s;
    ms_scanner_init(&s, "\"a ${f({1:2})} b\"");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STRING_INTERP);   /* "a " */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);      /* f */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LEFT_PAREN);      /* ( */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LEFT_BRACE);      /* { */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NUMBER_INT);      /* 1 */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_COLON);           /* : */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NUMBER_INT);      /* 2 */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RIGHT_BRACE);     /* } (map close, NOT interp close) */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RIGHT_PAREN);     /* ) */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STRING_INTERP_END); /* } closes interp, then " b" */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_EOF_TOKEN);
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
    test_save_restore_plain();
    test_save_restore_inside_interp();
    test_unterminated_block_comment();
    test_interp_nested_braces();
    printf("test_scanner_interp: all passed\n");
    return 0;
}
