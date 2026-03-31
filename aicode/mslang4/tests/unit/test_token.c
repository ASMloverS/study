#include "token.h"
#include <stdio.h>
#include <stdlib.h>

static void test_token_count(void) {
    if (MS_TOKEN_EOF + 1 != 50) {
        fprintf(stderr, "FAIL: expected 50 token types, got %d\n",
                MS_TOKEN_EOF + 1);
        exit(1);
    }
}

static void test_token_struct(void) {
    MsToken tok;
    tok.type = MS_TOKEN_NUMBER;
    tok.start = "42";
    tok.length = 2;
    tok.line = 1;
    tok.column = 5;

    if (tok.type != MS_TOKEN_NUMBER) {
        fprintf(stderr, "FAIL: type field not set correctly\n");
        exit(1);
    }
    if (tok.start[0] != '4' || tok.start[1] != '2') {
        fprintf(stderr, "FAIL: start field not set correctly\n");
        exit(1);
    }
    if (tok.length != 2) {
        fprintf(stderr, "FAIL: length field not set correctly\n");
        exit(1);
    }
    if (tok.line != 1) {
        fprintf(stderr, "FAIL: line field not set correctly\n");
        exit(1);
    }
    if (tok.column != 5) {
        fprintf(stderr, "FAIL: column field not set correctly\n");
        exit(1);
    }
}

static void test_enum_ordering(void) {
    if (MS_TOKEN_LEFT_PAREN != 0) {
        fprintf(stderr, "FAIL: MS_TOKEN_LEFT_PAREN should be 0\n");
        exit(1);
    }
    if (MS_TOKEN_BANG_EQUAL <= MS_TOKEN_BANG) {
        fprintf(stderr, "FAIL: compound operators should come after single-char\n");
        exit(1);
    }
    if (MS_TOKEN_ERROR >= MS_TOKEN_EOF) {
        fprintf(stderr, "FAIL: EOF should be last token\n");
        exit(1);
    }
    if (MS_TOKEN_LEFT_BRACE != MS_TOKEN_RIGHT_PAREN + 1) {
        fprintf(stderr, "FAIL: tokens should be sequential\n");
        exit(1);
    }
}

static void test_token_size(void) {
    if (sizeof(MsToken) > 32) {
        fprintf(stderr, "FAIL: MsToken is unexpectedly large: %zu bytes\n",
                sizeof(MsToken));
        exit(1);
    }
    if (sizeof(MsToken) < sizeof(MsTokenType) + sizeof(const char *) +
                          sizeof(int) * 3) {
        fprintf(stderr, "FAIL: MsToken is too small, fields may be missing\n");
        exit(1);
    }
}

int main(void) {
    test_token_count();
    printf("test_token_count passed\n");
    test_token_struct();
    printf("test_token_struct passed\n");
    test_enum_ordering();
    printf("test_enum_ordering passed\n");
    test_token_size();
    printf("test_token_size passed\n");
    return 0;
}
