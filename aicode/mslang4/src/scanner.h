#ifndef MS_SCANNER_H
#define MS_SCANNER_H

#include "token.h"

typedef struct {
	const char *start;
	const char *current;
	int line;
	int column;
} MsScanner;

void ms_scanner_init(MsScanner *scanner, const char *source);
MsToken ms_scanner_scan_token(MsScanner *scanner);

#endif
