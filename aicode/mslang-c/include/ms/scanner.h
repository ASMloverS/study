#pragma once
#include "ms/token.h"

typedef struct {
    const char* source;
    const char* start;        /* start of current token */
    const char* current;      /* current read position */
    int         line;
    int         column;
    int         start_line;   /* line at token start, for make_token */
    int         start_column; /* column at token start, for make_token */
    int         paren_depth;   /* paren depth, suppresses ASI inside parens */
    int         bracket_depth; /* bracket depth, suppresses ASI */
    int         brace_depth;   /* brace depth, suppresses ASI inside map literals */
    int         interp_depth;  /* string interpolation nesting */
    MsTokenType prev_type;     /* for ASI determination */
} MsScanner;

void    ms_scanner_init(MsScanner* s, const char* source);
MsToken ms_scanner_next(MsScanner* s);

typedef struct {
    const char* current;
    int         line;
    int         column;
    int         paren_depth;
    int         bracket_depth;
    int         brace_depth;
    int         interp_depth;
    MsTokenType prev_type;
} MsScannerState;

MsScannerState ms_scanner_save(const MsScanner* s);
void           ms_scanner_restore(MsScanner* s, MsScannerState st);
