#include "ms/scanner.h"
#include "ms/common.h"
#include <string.h>

/* --- token type name table --- */

static const char* const k_token_names[] = {
#define X(name) #name,
    MS_TOKEN_TYPES(X)
#undef X
};

const char* ms_token_type_name(MsTokenType type) {
    if (type < 0 || type >= MS_TK_COUNT) return "UNKNOWN";
    return k_token_names[type];
}

/* --- init / save / restore --- */

void ms_scanner_init(MsScanner* s, const char* source) {
    s->source        = source;
    s->start         = source;
    s->current       = source;
    s->line          = 1;
    s->column        = 1;
    s->start_line    = 1;
    s->start_column  = 1;
    s->paren_depth   = 0;
    s->bracket_depth = 0;
    s->brace_depth   = 0;
    s->interp_depth  = 0;
    s->prev_type     = MS_TK_EOF_TOKEN;
}

MsScannerState ms_scanner_save(const MsScanner* s) {
    MsScannerState st;
    st.current       = s->current;
    st.line          = s->line;
    st.column        = s->column;
    st.paren_depth   = s->paren_depth;
    st.bracket_depth = s->bracket_depth;
    st.brace_depth   = s->brace_depth;
    st.interp_depth  = s->interp_depth;
    st.prev_type     = s->prev_type;
    return st;
}

void ms_scanner_restore(MsScanner* s, MsScannerState st) {
    s->current       = st.current;
    s->line          = st.line;
    s->column        = st.column;
    s->paren_depth   = st.paren_depth;
    s->bracket_depth = st.bracket_depth;
    s->brace_depth   = st.brace_depth;
    s->interp_depth  = st.interp_depth;
    s->prev_type     = st.prev_type;
    s->start         = st.current;
}

/* --- helpers --- */

static bool is_at_end(const MsScanner* s) { return *s->current == '\0'; }

static char peek(const MsScanner* s)  { return *s->current; }
static char peek2(const MsScanner* s) { return is_at_end(s) ? '\0' : s->current[1]; }

static char advance(MsScanner* s) {
    char c = *s->current++;
    if (c == '\n') { s->line++; s->column = 1; }
    else            { s->column++; }
    return c;
}

static bool match(MsScanner* s, char expected) {
    if (is_at_end(s) || *s->current != expected) return false;
    advance(s);
    return true;
}

static MsToken make_token(MsScanner* s, MsTokenType type) {
    MsToken t;
    t.type   = type;
    t.start  = s->start;
    t.length = (int)(s->current - s->start);
    t.line   = s->start_line;
    t.column = s->start_column;
    s->prev_type = type;
    return t;
}

static MsToken error_token(MsScanner* s, const char* msg) {
    MsToken t;
    t.type   = MS_TK_ERROR;
    t.start  = msg;
    t.length = (int)strlen(msg);
    t.line   = s->start_line;
    t.column = s->start_column;
    s->prev_type = MS_TK_ERROR;
    return t;
}

/* --- keyword lookup --- */

static MsTokenType check_keyword(const char* start, int len,
                                 const char* rest, int rlen,
                                 MsTokenType type) {
    if (len == 1 + rlen && memcmp(start + 1, rest, (size_t)rlen) == 0)
        return type;
    return MS_TK_IDENTIFIER;
}

static MsTokenType identifier_type(const char* start, int len) {
    switch (start[0]) {
    case 'a':
        if (len > 1 && start[1] == 'n') return check_keyword(start, len, "nd", 2, MS_TK_AND);
        if (len > 1 && start[1] == 's') return (len == 2) ? MS_TK_AS : MS_TK_IDENTIFIER;
        break;
    case 'b': return check_keyword(start, len, "reak",     4, MS_TK_BREAK);
    case 'c':
        if (len > 1) switch (start[1]) {
        case 'a':
            if (len == 4 && memcmp(start,"case",4)==0) return MS_TK_CASE;
            return check_keyword(start, len, "atch",   4, MS_TK_CATCH);
        case 'l': return check_keyword(start, len, "lass",   4, MS_TK_CLASS);
        case 'o': return check_keyword(start, len, "ontinue",7, MS_TK_CONTINUE);
        default: break;
        }
        break;
    case 'd':
        if (len > 1 && start[1] == 'e') {
            if (len == 5) return check_keyword(start, len, "efer",   4, MS_TK_DEFER);
            if (len == 7) return check_keyword(start, len, "efault", 6, MS_TK_DEFAULT);
        }
        break;
    case 'e':
        if (len > 1 && start[1] == 'l') return check_keyword(start, len, "lse",  3, MS_TK_ELSE);
        if (len > 1 && start[1] == 'n') return check_keyword(start, len, "num",  3, MS_TK_ENUM);
        break;
    case 'f':
        if (len > 1) switch (start[1]) {
        case 'a': return check_keyword(start, len, "alse",  4, MS_TK_FALSE);
        case 'o': return check_keyword(start, len, "or",    2, MS_TK_FOR);
        case 'r': return check_keyword(start, len, "rom",   3, MS_TK_FROM);
        case 'u': return check_keyword(start, len, "un",    2, MS_TK_FUN);
        default: break;
        }
        break;
    case 'i':
        if (len > 1) switch (start[1]) {
        case 'f': return check_keyword(start, len, "f",      1, MS_TK_IF);
        case 'm': return check_keyword(start, len, "mport",  5, MS_TK_IMPORT);
        case 'n': return (len == 2) ? MS_TK_IN : MS_TK_IDENTIFIER;
        default: break;
        }
        break;
    case 'n':
        if (len > 1 && start[1] == 'i') return check_keyword(start, len, "il", 2, MS_TK_NIL);
        if (len > 1 && start[1] == 'o') return check_keyword(start, len, "ot", 2, MS_TK_NOT_KW);
        break;
    case 'o': return check_keyword(start, len, "r",        1, MS_TK_OR);
    case 'p': return check_keyword(start, len, "rint",     4, MS_TK_PRINT);
    case 'r': return check_keyword(start, len, "eturn",    5, MS_TK_RETURN);
    case 's':
        if (len > 1) switch (start[1]) {
        case 'u': return check_keyword(start, len, "uper",   4, MS_TK_SUPER);
        case 't': return check_keyword(start, len, "tatic",  5, MS_TK_STATIC);
        case 'w': return check_keyword(start, len, "witch",  5, MS_TK_SWITCH);
        default: break;
        }
        break;
    case 't':
        if (len > 1) switch (start[1]) {
        case 'h':
            if (len == 4) return check_keyword(start, len, "his",  3, MS_TK_THIS);
            return check_keyword(start, len, "hrow", 4, MS_TK_THROW);
        case 'r':
            if (len == 3) return check_keyword(start, len, "ry",   2, MS_TK_TRY);
            return check_keyword(start, len, "rue",  3, MS_TK_TRUE);
        default: break;
        }
        break;
    case 'v': return check_keyword(start, len, "ar",       2, MS_TK_VAR);
    case 'w': return check_keyword(start, len, "hile",     4, MS_TK_WHILE);
    case 'y': return check_keyword(start, len, "ield",     4, MS_TK_YIELD);
    default:  break;
    }
    return MS_TK_IDENTIFIER;
}

/* --- lexing helpers --- */

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool is_hex_digit(char c) {
    return is_digit(c) || (c>='a'&&c<='f') || (c>='A'&&c<='F');
}

static MsToken scan_number(MsScanner* s) {
    bool is_float = false;
    char first = s->start[0];
    if (first == '0' && (peek(s) == 'x' || peek(s) == 'X')) {
        advance(s);
        if (!is_hex_digit(peek(s))) return error_token(s, "Expected hex digit after '0x'.");
        while (is_hex_digit(peek(s))) advance(s);
        return make_token(s, MS_TK_NUMBER_INT);
    }
    if (first == '0' && (peek(s) == 'b' || peek(s) == 'B')) {
        advance(s);
        if (peek(s) != '0' && peek(s) != '1') return error_token(s, "Expected binary digit after '0b'.");
        while (peek(s) == '0' || peek(s) == '1') advance(s);
        return make_token(s, MS_TK_NUMBER_INT);
    }
    if (first == '0' && (peek(s) == 'o' || peek(s) == 'O')) {
        advance(s);
        if (peek(s) < '0' || peek(s) > '7') return error_token(s, "Expected octal digit after '0o'.");
        while (peek(s) >= '0' && peek(s) <= '7') advance(s);
        return make_token(s, MS_TK_NUMBER_INT);
    }
    while (is_digit(peek(s))) advance(s);
    if (peek(s) == '.' && is_digit(peek2(s))) {
        is_float = true;
        advance(s);
        while (is_digit(peek(s))) advance(s);
    }
    if (peek(s) == 'e' || peek(s) == 'E') {
        is_float = true;
        advance(s);
        if (peek(s) == '+' || peek(s) == '-') advance(s);
        if (!is_digit(peek(s))) return error_token(s, "Expected digit in exponent.");
        while (is_digit(peek(s))) advance(s);
    }
    return make_token(s, is_float ? MS_TK_NUMBER_FLOAT : MS_TK_NUMBER_INT);
}

static MsToken scan_string(MsScanner* s) {
    while (!is_at_end(s) && peek(s) != '"') {
        if (peek(s) == '\n') { advance(s); continue; }
        if (peek(s) == '\\') {
            advance(s);
            if (is_at_end(s)) return error_token(s, "Unterminated string.");
            char esc = peek(s);
            if (esc != 'n' && esc != 't' && esc != 'r' &&
                esc != '\\' && esc != '"' && esc != '0') {
                advance(s);
                return error_token(s, "Invalid escape sequence.");
            }
        }
        if (!is_at_end(s)) advance(s);
    }
    if (is_at_end(s)) return error_token(s, "Unterminated string.");
    advance(s);
    return make_token(s, MS_TK_STRING);
}

static MsToken scan_identifier(MsScanner* s) {
    while (is_alpha(peek(s)) || is_digit(peek(s))) advance(s);
    int len = (int)(s->current - s->start);
    MsTokenType type = identifier_type(s->start, len);
    return make_token(s, type);
}

/* --- ASI --- */

static bool asi_prev_triggers(MsTokenType t) {
    switch (t) {
    case MS_TK_IDENTIFIER:
    case MS_TK_NUMBER_INT:
    case MS_TK_NUMBER_FLOAT:
    case MS_TK_STRING:
    case MS_TK_STRING_INTERP_END:
    case MS_TK_RIGHT_PAREN:
    case MS_TK_RIGHT_BRACKET:
    case MS_TK_RIGHT_BRACE:
    case MS_TK_TRUE:
    case MS_TK_FALSE:
    case MS_TK_NIL:
    case MS_TK_RETURN:
    case MS_TK_BREAK:
    case MS_TK_CONTINUE:
        return true;
    default:
        return false;
    }
}

/* --- skip whitespace and comments --- */

static void skip_line_comment(MsScanner* s) {
    while (!is_at_end(s) && peek(s) != '\n') advance(s);
}

static void skip_block_comment(MsScanner* s) {
    while (!is_at_end(s)) {
        if (peek(s) == '*' && peek2(s) == '/') {
            advance(s); advance(s); return;
        }
        advance(s);
    }
}

static bool skip_whitespace_check_asi(MsScanner* s) {
    for (;;) {
        char c = peek(s);
        switch (c) {
        case ' ': case '\r': case '\t':
            advance(s);
            break;
        case '\n':
            if (s->paren_depth == 0 && s->bracket_depth == 0
                    && asi_prev_triggers(s->prev_type)) {
                return true;
            }
            /* consume newline without ASI */
            advance(s);
            break;
        case '/':
            if (peek2(s) == '/') {
                skip_line_comment(s);
            } else if (peek2(s) == '*') {
                advance(s); advance(s);
                skip_block_comment(s);
            } else {
                return false;
            }
            break;
        default:
            return false;
        }
    }
}

/* --- main scanner entry point --- */

MsToken ms_scanner_next(MsScanner* s) {
    bool emit_asi = skip_whitespace_check_asi(s);
    s->start        = s->current;
    s->start_line   = s->line;
    s->start_column = s->column;

    if (emit_asi) {
        MsToken t;
        t.type   = MS_TK_NEWLINE;
        t.start  = s->current;
        t.length = 1;
        t.line   = s->line;
        t.column = s->column;
        advance(s);
        s->prev_type = MS_TK_NEWLINE;
        return t;
    }

    if (is_at_end(s)) {
        s->prev_type = MS_TK_EOF_TOKEN;
        MsToken t;
        t.type = MS_TK_EOF_TOKEN; t.start = s->current;
        t.length = 0; t.line = s->line; t.column = s->column;
        return t;
    }

    char c = advance(s);

    if (is_alpha(c)) return scan_identifier(s);
    if (is_digit(c)) return scan_number(s);

    switch (c) {
    case '(': s->paren_depth++;   return make_token(s, MS_TK_LEFT_PAREN);
    case ')': s->paren_depth--;   return make_token(s, MS_TK_RIGHT_PAREN);
    case '[': s->bracket_depth++; return make_token(s, MS_TK_LEFT_BRACKET);
    case ']': s->bracket_depth--; return make_token(s, MS_TK_RIGHT_BRACKET);
    case '{': s->brace_depth++;   return make_token(s, MS_TK_LEFT_BRACE);
    case '}': if (s->brace_depth > 0) s->brace_depth--;
              return make_token(s, MS_TK_RIGHT_BRACE);
    case ',': return make_token(s, MS_TK_COMMA);
    case ';': return make_token(s, MS_TK_SEMICOLON);
    case ':': return make_token(s, MS_TK_COLON);
    case '?': return make_token(s, MS_TK_QUESTION);
    case '~': return make_token(s, MS_TK_TILDE);
    case '&': return make_token(s, MS_TK_AMPERSAND);
    case '|': return make_token(s, MS_TK_PIPE);
    case '^': return make_token(s, MS_TK_CARET);
    case '.': return match(s,'.') ? make_token(s,MS_TK_DOT_DOT) : make_token(s,MS_TK_DOT);
    case '!': return make_token(s, match(s,'=') ? MS_TK_BANG_EQUAL   : MS_TK_BANG);
    case '=': return make_token(s, match(s,'=') ? MS_TK_EQUAL_EQUAL  : MS_TK_EQUAL);
    case '>': return make_token(s, match(s,'=') ? MS_TK_GREATER_EQUAL
                                  : match(s,'>') ? MS_TK_GREATER_GREATER : MS_TK_GREATER);
    case '<': return make_token(s, match(s,'=') ? MS_TK_LESS_EQUAL
                                  : match(s,'<') ? MS_TK_LESS_LESS       : MS_TK_LESS);
    case '+': return make_token(s, match(s,'=') ? MS_TK_PLUS_EQUAL    : MS_TK_PLUS);
    case '-': return make_token(s, match(s,'=') ? MS_TK_MINUS_EQUAL   : MS_TK_MINUS);
    case '*': return make_token(s, match(s,'=') ? MS_TK_STAR_EQUAL    : MS_TK_STAR);
    case '/': return make_token(s, match(s,'=') ? MS_TK_SLASH_EQUAL   : MS_TK_SLASH);
    case '%': return make_token(s, match(s,'=') ? MS_TK_PERCENT_EQUAL : MS_TK_PERCENT);
    case '"': return scan_string(s);
    default:  return error_token(s, "Unexpected character.");
    }
}
