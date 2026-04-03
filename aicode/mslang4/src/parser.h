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
	bool had_error;
	bool panic_mode;
	MsParseError last_error;
} MsParser;

void ms_parser_init(MsParser *parser, const char *source);
int ms_parser_parse(MsParser *parser, MsStmt ***out_statements);
bool ms_parser_had_error(const MsParser *parser);

#endif
