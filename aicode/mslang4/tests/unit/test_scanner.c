#include "scanner.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_empty_input(void) {
	MsScanner scanner;
	ms_scanner_init(&scanner, "");
	MsToken tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_EOF) {
		fprintf(stderr, "FAIL: empty input should be EOF, got %d\n",
			tok.type);
		exit(1);
	}
	if (tok.line != 1) {
		fprintf(stderr, "FAIL: EOF line should be 1, got %d\n", tok.line);
		exit(1);
	}
	if (tok.column != 1) {
		fprintf(stderr, "FAIL: EOF column should be 1, got %d\n",
			tok.column);
		exit(1);
	}
	printf("  test_empty_input PASSED\n");
}

static void test_single_char_tokens(void) {
	struct {
		const char *source;
		MsTokenType type;
	} cases[] = {
		{"(", MS_TOKEN_LEFT_PAREN},
		{")", MS_TOKEN_RIGHT_PAREN},
		{"{", MS_TOKEN_LEFT_BRACE},
		{"}", MS_TOKEN_RIGHT_BRACE},
		{"[", MS_TOKEN_LEFT_BRACKET},
		{"]", MS_TOKEN_RIGHT_BRACKET},
		{";", MS_TOKEN_SEMICOLON},
		{":", MS_TOKEN_COLON},
		{",", MS_TOKEN_COMMA},
		{".", MS_TOKEN_DOT},
		{"+", MS_TOKEN_PLUS},
		{"-", MS_TOKEN_MINUS},
		{"*", MS_TOKEN_STAR},
		{"/", MS_TOKEN_SLASH},
		{"%", MS_TOKEN_PERCENT},
		{"!", MS_TOKEN_BANG},
		{"=", MS_TOKEN_EQUAL},
		{"<", MS_TOKEN_LESS},
		{">", MS_TOKEN_GREATER},
	};
	int n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++) {
		MsScanner scanner;
		ms_scanner_init(&scanner, cases[i].source);
		MsToken tok = ms_scanner_scan_token(&scanner);
		if (tok.type != cases[i].type) {
			fprintf(stderr,
				"FAIL: single_char '%s' expected %d, got %d\n",
				cases[i].source, cases[i].type, tok.type);
			exit(1);
		}
	}
	printf("  test_single_char_tokens PASSED\n");
}

static void test_two_char_tokens(void) {
	struct {
		const char *source;
		MsTokenType type;
	} cases[] = {
		{"==", MS_TOKEN_EQUAL_EQUAL},
		{"!=", MS_TOKEN_BANG_EQUAL},
		{"<=", MS_TOKEN_LESS_EQUAL},
		{">=", MS_TOKEN_GREATER_EQUAL},
	};
	int n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++) {
		MsScanner scanner;
		ms_scanner_init(&scanner, cases[i].source);
		MsToken tok = ms_scanner_scan_token(&scanner);
		if (tok.type != cases[i].type) {
			fprintf(stderr,
				"FAIL: two_char '%s' expected %d, got %d\n",
				cases[i].source, cases[i].type, tok.type);
			exit(1);
		}
	}
	printf("  test_two_char_tokens PASSED\n");
}

static void test_number_literals(void) {
	MsScanner scanner;
	ms_scanner_init(&scanner, "123");
	MsToken tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_NUMBER) {
		fprintf(stderr, "FAIL: '123' expected NUMBER, got %d\n",
			tok.type);
		exit(1);
	}
	if (tok.length != 3) {
		fprintf(stderr, "FAIL: '123' length expected 3, got %d\n",
			tok.length);
		exit(1);
	}
	if (strncmp(tok.start, "123", 3) != 0) {
		fprintf(stderr, "FAIL: '123' lexeme mismatch\n");
		exit(1);
	}

	ms_scanner_init(&scanner, "3.14");
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_NUMBER) {
		fprintf(stderr, "FAIL: '3.14' expected NUMBER, got %d\n",
			tok.type);
		exit(1);
	}
	if (tok.length != 4) {
		fprintf(stderr, "FAIL: '3.14' length expected 4, got %d\n",
			tok.length);
		exit(1);
	}
	if (strncmp(tok.start, "3.14", 4) != 0) {
		fprintf(stderr, "FAIL: '3.14' lexeme mismatch\n");
		exit(1);
	}
	printf("  test_number_literals PASSED\n");
}

static void test_string_literals(void) {
	MsScanner scanner;
	ms_scanner_init(&scanner, "\"hello\"");
	MsToken tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_STRING) {
		fprintf(stderr, "FAIL: '\"hello\"' expected STRING, got %d\n",
			tok.type);
		exit(1);
	}
	if (tok.length != 7) {
		fprintf(stderr,
			"FAIL: '\"hello\"' length expected 7, got %d\n",
			tok.length);
		exit(1);
	}

	ms_scanner_init(&scanner, "\"a\\\"b\"");
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_STRING) {
		fprintf(stderr, "FAIL: escaped quote expected STRING, got %d\n",
			tok.type);
		exit(1);
	}

	ms_scanner_init(&scanner, "\"\\n\\t\\\\\"");
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_STRING) {
		fprintf(stderr,
			"FAIL: escape sequences expected STRING, got %d\n",
			tok.type);
		exit(1);
	}
	printf("  test_string_literals PASSED\n");
}

static void test_identifiers(void) {
	MsScanner scanner;
	ms_scanner_init(&scanner, "myVar");
	MsToken tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_IDENTIFIER) {
		fprintf(stderr, "FAIL: 'myVar' expected IDENTIFIER, got %d\n",
			tok.type);
		exit(1);
	}
	if (tok.length != 5) {
		fprintf(stderr, "FAIL: 'myVar' length expected 5, got %d\n",
			tok.length);
		exit(1);
	}
	if (strncmp(tok.start, "myVar", 5) != 0) {
		fprintf(stderr, "FAIL: 'myVar' lexeme mismatch\n");
		exit(1);
	}

	ms_scanner_init(&scanner, "_foo_bar2");
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_IDENTIFIER) {
		fprintf(stderr,
			"FAIL: '_foo_bar2' expected IDENTIFIER, got %d\n",
			tok.type);
		exit(1);
	}
	if (tok.length != 9) {
		fprintf(stderr,
			"FAIL: '_foo_bar2' length expected 9, got %d\n",
			tok.length);
		exit(1);
	}
	printf("  test_identifiers PASSED\n");
}

static void test_keywords(void) {
	struct {
		const char *word;
		MsTokenType type;
	} keywords[] = {
		{"and", MS_TOKEN_AND}, {"class", MS_TOKEN_CLASS},
		{"else", MS_TOKEN_ELSE}, {"false", MS_TOKEN_FALSE},
		{"fn", MS_TOKEN_FN}, {"for", MS_TOKEN_FOR},
		{"if", MS_TOKEN_IF}, {"nil", MS_TOKEN_NIL},
		{"or", MS_TOKEN_OR}, {"print", MS_TOKEN_PRINT},
		{"return", MS_TOKEN_RETURN}, {"super", MS_TOKEN_SUPER},
		{"this", MS_TOKEN_THIS}, {"true", MS_TOKEN_TRUE},
		{"var", MS_TOKEN_VAR}, {"while", MS_TOKEN_WHILE},
		{"break", MS_TOKEN_BREAK}, {"continue", MS_TOKEN_CONTINUE},
		{"import", MS_TOKEN_IMPORT}, {"from", MS_TOKEN_FROM},
		{"as", MS_TOKEN_AS},
	};
	int n = sizeof(keywords) / sizeof(keywords[0]);
	for (int i = 0; i < n; i++) {
		MsScanner scanner;
		ms_scanner_init(&scanner, keywords[i].word);
		MsToken tok = ms_scanner_scan_token(&scanner);
		if (tok.type != keywords[i].type) {
			fprintf(stderr,
				"FAIL: keyword '%s' expected %d, got %d\n",
				keywords[i].word, keywords[i].type, tok.type);
			exit(1);
		}
	}
	printf("  test_keywords PASSED\n");
}

static void test_whitespace_comments(void) {
	MsScanner scanner;
	ms_scanner_init(&scanner, "// comment\n42");
	MsToken tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_NUMBER) {
		fprintf(stderr,
			"FAIL: line comment expected NUMBER, got %d\n",
			tok.type);
		exit(1);
	}
	if (tok.line != 2) {
		fprintf(stderr,
			"FAIL: line comment expected line 2, got %d\n",
			tok.line);
		exit(1);
	}

	ms_scanner_init(&scanner, "/* block */ 42");
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_NUMBER) {
		fprintf(stderr,
			"FAIL: block comment expected NUMBER, got %d\n",
			tok.type);
		exit(1);
	}

	ms_scanner_init(&scanner, "   \t\r\n  42");
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_NUMBER) {
		fprintf(stderr,
			"FAIL: whitespace skip expected NUMBER, got %d\n",
			tok.type);
		exit(1);
	}
	if (tok.line != 2) {
		fprintf(stderr,
			"FAIL: whitespace skip expected line 2, got %d\n",
			tok.line);
		exit(1);
	}
	printf("  test_whitespace_comments PASSED\n");
}

static void test_multi_token(void) {
	MsScanner scanner;
	ms_scanner_init(&scanner, "var x = 42");
	MsToken tok;

	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_VAR) {
		fprintf(stderr, "FAIL: 'var' expected VAR, got %d\n",
			tok.type);
		exit(1);
	}
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_IDENTIFIER) {
		fprintf(stderr, "FAIL: 'x' expected IDENTIFIER, got %d\n",
			tok.type);
		exit(1);
	}
	if (strncmp(tok.start, "x", 1) != 0) {
		fprintf(stderr, "FAIL: 'x' lexeme mismatch\n");
		exit(1);
	}
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_EQUAL) {
		fprintf(stderr, "FAIL: '=' expected EQUAL, got %d\n",
			tok.type);
		exit(1);
	}
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_NUMBER) {
		fprintf(stderr, "FAIL: '42' expected NUMBER, got %d\n",
			tok.type);
		exit(1);
	}
	if (strncmp(tok.start, "42", 2) != 0) {
		fprintf(stderr, "FAIL: '42' lexeme mismatch\n");
		exit(1);
	}
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_EOF) {
		fprintf(stderr, "FAIL: expected EOF, got %d\n", tok.type);
		exit(1);
	}
	printf("  test_multi_token PASSED\n");
}

static void test_line_column(void) {
	MsScanner scanner;
	ms_scanner_init(&scanner, "1\n+\n2");
	MsToken tok;

	tok = ms_scanner_scan_token(&scanner);
	if (tok.line != 1) {
		fprintf(stderr, "FAIL: '1' expected line 1, got %d\n",
			tok.line);
		exit(1);
	}
	tok = ms_scanner_scan_token(&scanner);
	if (tok.line != 2) {
		fprintf(stderr, "FAIL: '+' expected line 2, got %d\n",
			tok.line);
		exit(1);
	}
	if (tok.type != MS_TOKEN_PLUS) {
		fprintf(stderr, "FAIL: '+' expected PLUS, got %d\n",
			tok.type);
		exit(1);
	}
	tok = ms_scanner_scan_token(&scanner);
	if (tok.line != 3) {
		fprintf(stderr, "FAIL: '2' expected line 3, got %d\n",
			tok.line);
		exit(1);
	}
	printf("  test_line_column PASSED\n");
}

static void test_error_tokens(void) {
	MsScanner scanner;
	ms_scanner_init(&scanner, "@");
	MsToken tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_ERROR) {
		fprintf(stderr, "FAIL: '@' expected ERROR, got %d\n",
			tok.type);
		exit(1);
	}

	ms_scanner_init(&scanner, "#");
	tok = ms_scanner_scan_token(&scanner);
	if (tok.type != MS_TOKEN_ERROR) {
		fprintf(stderr, "FAIL: '#' expected ERROR, got %d\n",
			tok.type);
		exit(1);
	}
	printf("  test_error_tokens PASSED\n");
}

int main(void) {
	printf("Running scanner tests...\n");
	test_empty_input();
	test_single_char_tokens();
	test_two_char_tokens();
	test_number_literals();
	test_string_literals();
	test_identifiers();
	test_keywords();
	test_whitespace_comments();
	test_multi_token();
	test_line_column();
	test_error_tokens();
	printf("All scanner tests passed.\n");
	return 0;
}
