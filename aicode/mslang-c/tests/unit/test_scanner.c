#include "../../tests/test_assert.h"
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
    /* No NEWLINE inside parens */
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NUMBER_INT);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RIGHT_PAREN);
}

static void test_string_escapes(void) {
    MsScanner s;
    ms_scanner_init(&s, "\"hello\\nworld\"");
    MsToken t = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t.type, MS_TK_STRING);
    /* t.start points to opening quote, t.length includes quotes */
    TEST_ASSERT(t.length > 0);
}

static void test_single_char_tokens(void) {
    MsScanner s;
    ms_scanner_init(&s, "( ) { } [ ] , . ; : ?");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LEFT_PAREN);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RIGHT_PAREN);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LEFT_BRACE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RIGHT_BRACE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LEFT_BRACKET);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RIGHT_BRACKET);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_COMMA);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_DOT);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_SEMICOLON);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_COLON);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_QUESTION);
}

static void test_two_char_tokens(void) {
    MsScanner s;
    ms_scanner_init(&s, "!= == >= <= >> << += -= *= /=");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_BANG_EQUAL);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_EQUAL_EQUAL);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_GREATER_EQUAL);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LESS_EQUAL);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_GREATER_GREATER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LESS_LESS);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_PLUS_EQUAL);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_MINUS_EQUAL);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STAR_EQUAL);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_SLASH_EQUAL);
}

static void test_line_comment_skipped(void) {
    MsScanner s;
    ms_scanner_init(&s, "1 // comment\n2");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NUMBER_INT);
    /* NEWLINE from ASI after '1' on the line with comment */
    MsToken t = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t.type, MS_TK_NEWLINE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NUMBER_INT);
}

static void test_hex_number(void) {
    MsScanner s;
    ms_scanner_init(&s, "0xFF");
    MsToken t = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t.type, MS_TK_NUMBER_INT);
    TEST_ASSERT_EQ(t.length, 4);
}

static void test_identifier_vs_keyword(void) {
    MsScanner s;
    ms_scanner_init(&s, "iffy variable");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
}

static void test_save_restore(void) {
    MsScanner s;
    ms_scanner_init(&s, "a b c");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER); /* a */
    MsScannerState st = ms_scanner_save(&s);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER); /* b */
    ms_scanner_restore(&s, st);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER); /* b again */
}

static void test_token_type_name(void) {
    TEST_ASSERT(ms_token_type_name(MS_TK_PLUS) != NULL);
    TEST_ASSERT(ms_token_type_name(MS_TK_EOF_TOKEN) != NULL);
}

static void test_line_column_tracking(void) {
    MsScanner s;
    ms_scanner_init(&s, "a\nb");
    MsToken t1 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t1.line, 1);
    TEST_ASSERT_EQ(t1.column, 1);
    ms_scanner_next(&s); /* NEWLINE */
    MsToken t2 = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t2.line, 2);
    TEST_ASSERT_EQ(t2.column, 1);
}

static void test_import_keyword(void) {
    MsScanner s;
    ms_scanner_init(&s, "import from as");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IMPORT);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_FROM);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_AS);
}

static void test_all_keywords(void) {
    MsScanner s;
    ms_scanner_init(&s, "and or not break continue in print try catch throw defer yield switch case default enum static this super true false nil");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_AND);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_OR);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NOT_KW);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_BREAK);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_CONTINUE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IN);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_PRINT);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_TRY);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_CATCH);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_THROW);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_DEFER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_YIELD);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_SWITCH);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_CASE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_DEFAULT);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_ENUM);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_STATIC);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_THIS);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_SUPER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_TRUE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_FALSE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NIL);
}

static void test_asi_emitted_in_braces(void) {
    /* Braces do NOT suppress ASI - statements inside blocks need newline tokens */
    MsScanner s;
    ms_scanner_init(&s, "{\nfoo\n}");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_LEFT_BRACE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_NEWLINE);
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_RIGHT_BRACE);
}

static void test_number_line(void) {
    MsScanner s;
    ms_scanner_init(&s, "42\n3.14");
    MsToken t = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t.type, MS_TK_NUMBER_INT);
    TEST_ASSERT_EQ(t.line, 1);  /* line 1 */
    ms_scanner_next(&s);  /* NEWLINE */
    t = ms_scanner_next(&s);
    TEST_ASSERT_EQ(t.type, MS_TK_NUMBER_FLOAT);
    TEST_ASSERT_EQ(t.line, 2);  /* line 2 */
}

int main(void) {
    test_arithmetic_tokens();
    test_keywords();
    test_asi();
    test_asi_suppressed_in_parens();
    test_string_escapes();
    test_single_char_tokens();
    test_two_char_tokens();
    test_line_comment_skipped();
    test_hex_number();
    test_identifier_vs_keyword();
    test_save_restore();
    test_token_type_name();
    test_line_column_tracking();
    test_import_keyword();
    test_all_keywords();
    test_asi_emitted_in_braces();
    test_number_line();
    printf("All scanner tests passed.\n");
    return 0;
}
