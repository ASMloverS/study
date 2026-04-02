#include "scanner.h"
#include "common.h"
#include <string.h>

static MsToken ms_make_token(MsScanner *scanner, MsTokenType type)
{
	MsToken tok;
	tok.type = type;
	tok.start = scanner->start;
	tok.length = (int)(scanner->current - scanner->start);
	tok.line = scanner->line;
	tok.column = scanner->column;
	return tok;
}

static MsToken ms_error_token(MsScanner *scanner, const char *message)
{
	MsToken tok;
	tok.type = MS_TOKEN_ERROR;
	tok.start = message;
	tok.length = (int)strlen(message);
	tok.line = scanner->line;
	tok.column = scanner->column;
	return tok;
}

static char ms_scanner_advance(MsScanner *scanner)
{
	char c = *scanner->current;
	scanner->current++;
	scanner->column++;
	return c;
}

static bool ms_scanner_at_end(MsScanner *scanner)
{
	return *scanner->current == '\0';
}

static char ms_scanner_peek(MsScanner *scanner)
{
	return *scanner->current;
}

static bool ms_scanner_match(MsScanner *scanner, char expected)
{
	if (ms_scanner_at_end(scanner))
		return false;
	if (*scanner->current != expected)
		return false;
	scanner->current++;
	scanner->column++;
	return true;
}

static bool ms_is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool ms_is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool ms_is_alphanumeric(char c)
{
	return ms_is_alpha(c) || ms_is_digit(c);
}

static MsToken ms_scan_number(MsScanner *scanner)
{
	while (ms_is_digit(ms_scanner_peek(scanner)))
		ms_scanner_advance(scanner);
	if (ms_scanner_peek(scanner) == '.' &&
	    ms_is_digit(*(scanner->current + 1))) {
		ms_scanner_advance(scanner);
		while (ms_is_digit(ms_scanner_peek(scanner)))
			ms_scanner_advance(scanner);
	}
	return ms_make_token(scanner, MS_TOKEN_NUMBER);
}

static MsToken ms_scan_string(MsScanner *scanner)
{
	while (ms_scanner_peek(scanner) != '"' && !ms_scanner_at_end(scanner)) {
		if (ms_scanner_peek(scanner) == '\n') {
			scanner->line++;
			scanner->column = 0;
		}
		if (ms_scanner_peek(scanner) == '\\')
			ms_scanner_advance(scanner);
		ms_scanner_advance(scanner);
	}
	if (ms_scanner_at_end(scanner))
		return ms_error_token(scanner, "Unterminated string.");
	ms_scanner_advance(scanner);
	return ms_make_token(scanner, MS_TOKEN_STRING);
}

static MsTokenType ms_check_keyword(MsScanner *scanner, int start, int length,
				   const char *rest, MsTokenType type)
{
	if (scanner->current - scanner->start == start + length &&
	    memcmp(scanner->start + start, rest, length) == 0)
		return type;
	return MS_TOKEN_IDENTIFIER;
}

static MsTokenType ms_identifier_type(MsScanner *scanner)
{
	switch (scanner->start[0]) {
	case 'a': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'n':
				return ms_check_keyword(scanner, 2, 1, "d",
							MS_TOKEN_AND);
			case 's':
				return ms_check_keyword(scanner, 2, 0, "",
							MS_TOKEN_AS);
			}
		}
		break;
	}
	case 'b':
		return ms_check_keyword(scanner, 1, 4, "reak", MS_TOKEN_BREAK);
	case 'c': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'l':
				return ms_check_keyword(scanner, 2, 3, "ass",
							MS_TOKEN_CLASS);
			case 'o':
				return ms_check_keyword(scanner, 2, 6,
							"ntinue",
							MS_TOKEN_CONTINUE);
			}
		}
		break;
	}
	case 'e':
		return ms_check_keyword(scanner, 1, 3, "lse", MS_TOKEN_ELSE);
	case 'f': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'a':
				return ms_check_keyword(scanner, 2, 3, "lse",
							MS_TOKEN_FALSE);
			case 'n':
				return ms_check_keyword(scanner, 2, 0, "",
							MS_TOKEN_FN);
			case 'o':
				return ms_check_keyword(scanner, 2, 1, "r",
							MS_TOKEN_FOR);
			case 'r':
				return ms_check_keyword(scanner, 2, 2, "om",
							MS_TOKEN_FROM);
			}
		}
		break;
	}
	case 'i': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'f':
				return ms_check_keyword(scanner, 2, 0, "",
							MS_TOKEN_IF);
			case 'm':
				return ms_check_keyword(scanner, 2, 4, "port",
							MS_TOKEN_IMPORT);
			}
		}
		break;
	}
	case 'n':
		return ms_check_keyword(scanner, 1, 2, "il", MS_TOKEN_NIL);
	case 'o':
		return ms_check_keyword(scanner, 1, 1, "r", MS_TOKEN_OR);
	case 'p':
		return ms_check_keyword(scanner, 1, 4, "rint", MS_TOKEN_PRINT);
	case 'r':
		return ms_check_keyword(scanner, 1, 5, "eturn",
					 MS_TOKEN_RETURN);
	case 's':
		return ms_check_keyword(scanner, 1, 4, "uper", MS_TOKEN_SUPER);
	case 't': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'h':
				return ms_check_keyword(scanner, 2, 2, "is",
							MS_TOKEN_THIS);
			case 'r':
				return ms_check_keyword(scanner, 2, 2, "ue",
							MS_TOKEN_TRUE);
			}
		}
		break;
	}
	case 'v':
		return ms_check_keyword(scanner, 1, 2, "ar", MS_TOKEN_VAR);
	case 'w':
		return ms_check_keyword(scanner, 1, 4, "hile",
					 MS_TOKEN_WHILE);
	}
	return MS_TOKEN_IDENTIFIER;
}

static MsToken ms_scan_identifier(MsScanner *scanner)
{
	while (ms_is_alphanumeric(ms_scanner_peek(scanner)))
		ms_scanner_advance(scanner);
	return ms_make_token(scanner, ms_identifier_type(scanner));
}

static void ms_skip_whitespace(MsScanner *scanner)
{
	for (;;) {
		char c = ms_scanner_peek(scanner);
		switch (c) {
		case ' ':
		case '\t':
		case '\r':
			ms_scanner_advance(scanner);
			break;
		case '\n':
			scanner->line++;
			scanner->column = 0;
			ms_scanner_advance(scanner);
			break;
		case '/':
			if (*(scanner->current + 1) == '/') {
				while (ms_scanner_peek(scanner) != '\n' &&
				       !ms_scanner_at_end(scanner))
					ms_scanner_advance(scanner);
			} else if (*(scanner->current + 1) == '*') {
				ms_scanner_advance(scanner);
				ms_scanner_advance(scanner);
				while (!ms_scanner_at_end(scanner)) {
					if (ms_scanner_peek(scanner) == '\n') {
						scanner->line++;
						scanner->column = 0;
					}
					if (ms_scanner_peek(scanner) == '*' &&
					    *(scanner->current + 1) == '/') {
						ms_scanner_advance(scanner);
						ms_scanner_advance(scanner);
						break;
					}
					ms_scanner_advance(scanner);
				}
			} else {
				return;
			}
			break;
		default:
			return;
		}
	}
}

void ms_scanner_init(MsScanner *scanner, const char *source)
{
	scanner->start = source;
	scanner->current = source;
	scanner->line = 1;
	scanner->column = 1;
}

MsToken ms_scanner_scan_token(MsScanner *scanner)
{
	ms_skip_whitespace(scanner);
	scanner->start = scanner->current;
	if (ms_scanner_at_end(scanner))
		return ms_make_token(scanner, MS_TOKEN_EOF);

	char c = ms_scanner_advance(scanner);
	switch (c) {
	case '(': return ms_make_token(scanner, MS_TOKEN_LEFT_PAREN);
	case ')': return ms_make_token(scanner, MS_TOKEN_RIGHT_PAREN);
	case '{': return ms_make_token(scanner, MS_TOKEN_LEFT_BRACE);
	case '}': return ms_make_token(scanner, MS_TOKEN_RIGHT_BRACE);
	case '[': return ms_make_token(scanner, MS_TOKEN_LEFT_BRACKET);
	case ']': return ms_make_token(scanner, MS_TOKEN_RIGHT_BRACKET);
	case ';': return ms_make_token(scanner, MS_TOKEN_SEMICOLON);
	case ':': return ms_make_token(scanner, MS_TOKEN_COLON);
	case ',': return ms_make_token(scanner, MS_TOKEN_COMMA);
	case '.': return ms_make_token(scanner, MS_TOKEN_DOT);
	case '+': return ms_make_token(scanner, MS_TOKEN_PLUS);
	case '-': return ms_make_token(scanner, MS_TOKEN_MINUS);
	case '*': return ms_make_token(scanner, MS_TOKEN_STAR);
	case '/': return ms_make_token(scanner, MS_TOKEN_SLASH);
	case '%': return ms_make_token(scanner, MS_TOKEN_PERCENT);
	case '"': return ms_scan_string(scanner);
	case '!': return ms_make_token(scanner,
		ms_scanner_match(scanner, '=') ? MS_TOKEN_BANG_EQUAL
					       : MS_TOKEN_BANG);
	case '=': return ms_make_token(scanner,
		ms_scanner_match(scanner, '=') ? MS_TOKEN_EQUAL_EQUAL
					       : MS_TOKEN_EQUAL);
	case '<': return ms_make_token(scanner,
		ms_scanner_match(scanner, '=') ? MS_TOKEN_LESS_EQUAL
					       : MS_TOKEN_LESS);
	case '>': return ms_make_token(scanner,
		ms_scanner_match(scanner, '=') ? MS_TOKEN_GREATER_EQUAL
					       : MS_TOKEN_GREATER);
	default:
		if (ms_is_digit(c))
			return ms_scan_number(scanner);
		if (ms_is_alpha(c))
			return ms_scan_identifier(scanner);
		return ms_error_token(scanner, "Unexpected character.");
	}
}
