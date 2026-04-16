#pragma once

#define MS_TOKEN_TYPES(X) \
    /* Single-char */ \
    X(LEFT_PAREN) X(RIGHT_PAREN) X(LEFT_BRACE) X(RIGHT_BRACE) \
    X(LEFT_BRACKET) X(RIGHT_BRACKET) \
    X(COMMA) X(DOT) X(SEMICOLON) X(COLON) X(QUESTION) \
    /* One or two char */ \
    X(BANG) X(BANG_EQUAL) \
    X(EQUAL) X(EQUAL_EQUAL) \
    X(GREATER) X(GREATER_EQUAL) X(GREATER_GREATER) \
    X(LESS) X(LESS_EQUAL) X(LESS_LESS) \
    X(PLUS) X(PLUS_EQUAL) \
    X(MINUS) X(MINUS_EQUAL) \
    X(STAR) X(STAR_EQUAL) \
    X(SLASH) X(SLASH_EQUAL) \
    X(PERCENT) X(PERCENT_EQUAL) \
    X(AMPERSAND) X(PIPE) X(CARET) X(TILDE) \
    X(DOT_DOT) \
    /* Literals */ \
    X(IDENTIFIER) X(STRING) X(STRING_INTERP) X(STRING_INTERP_END) \
    X(NUMBER_INT) X(NUMBER_FLOAT) \
    /* Keywords */ \
    X(AND) X(OR) X(NOT_KW) \
    X(IF) X(ELSE) X(WHILE) X(FOR) X(IN) X(BREAK) X(CONTINUE) X(RETURN) \
    X(VAR) X(FUN) X(CLASS) X(THIS) X(SUPER) X(STATIC) \
    X(TRUE) X(FALSE) X(NIL) \
    X(PRINT) X(IMPORT) X(FROM) X(AS) \
    X(TRY) X(CATCH) X(THROW) X(DEFER) X(YIELD) \
    X(SWITCH) X(CASE) X(DEFAULT) X(ENUM) \
    /* Virtual / special */ \
    X(NEWLINE) \
    X(ERROR) X(EOF_TOKEN)

typedef enum {
#define X(name) MS_TK_##name,
    MS_TOKEN_TYPES(X)
#undef X
    MS_TK_COUNT
} MsTokenType;

typedef struct {
    MsTokenType  type;
    const char*  start;   /* points into source buffer */
    int          length;
    int          line;
    int          column;
} MsToken;

const char* ms_token_type_name(MsTokenType type);
