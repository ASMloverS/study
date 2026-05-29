/* tests/unit/test_stdlib_unicode.c - STDLIB-31: unicode module */
#include "ms/vm.h"
#include <stdio.h>
#include <stdlib.h>

static MsInterpretResult run_snippet(const char* src) {
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) { fprintf(stderr, "OOM\n"); exit(1); }
    ms_vm_init(vm);
    MsInterpretResult r = ms_vm_interpret(vm, src, "<test>");
    ms_vm_free(vm);
    free(vm);
    return r;
}

static void check_ok(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_OK) {
        fprintf(stderr, "FAIL test_stdlib_unicode.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_unicode.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- is_letter ---- */
static void test_is_letter(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.is_letter(\"A\") == true)\n"
        "assert(unicode.is_letter(\"z\") == true)\n"
        "assert(unicode.is_letter(\"1\") == false)\n"
        "assert(unicode.is_letter(\" \") == false)\n"
        "assert(unicode.is_letter(65) == true)\n"   /* 'A' */
        "assert(unicode.is_letter(49) == false)\n"  /* '1' */
    );
}

/* ---- is_digit ---- */
static void test_is_digit(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.is_digit(\"0\") == true)\n"
        "assert(unicode.is_digit(\"9\") == true)\n"
        "assert(unicode.is_digit(\"a\") == false)\n"
        "assert(unicode.is_digit(48) == true)\n"  /* '0' */
        "assert(unicode.is_digit(57) == true)\n"  /* '9' */
        "assert(unicode.is_digit(58) == false)\n" /* ':' */
    );
}

/* ---- is_space ---- */
static void test_is_space(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.is_space(\" \") == true)\n"
        "assert(unicode.is_space(9) == true)\n"   /* tab */
        "assert(unicode.is_space(10) == true)\n"  /* newline */
        "assert(unicode.is_space(\"A\") == false)\n"
        "assert(unicode.is_space(\"x\") == false)\n"
    );
}

/* ---- is_upper / is_lower ---- */
static void test_case_predicates(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.is_upper(\"A\") == true)\n"
        "assert(unicode.is_upper(\"a\") == false)\n"
        "assert(unicode.is_lower(\"a\") == true)\n"
        "assert(unicode.is_lower(\"A\") == false)\n"
        "assert(unicode.is_upper(65) == true)\n"  /* 'A' */
        "assert(unicode.is_lower(97) == true)\n"  /* 'a' */
    );
}

/* ---- is_punct ---- */
static void test_is_punct(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.is_punct(\".\") == true)\n"
        "assert(unicode.is_punct(\",\") == true)\n"
        "assert(unicode.is_punct(\"a\") == false)\n"
        "assert(unicode.is_punct(\"1\") == false)\n"
    );
}

/* ---- is_graphic ---- */
static void test_is_graphic(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.is_graphic(\"A\") == true)\n"
        "assert(unicode.is_graphic(\"!\") == true)\n"
        "assert(unicode.is_graphic(0) == false)\n"   /* NUL */
        "assert(unicode.is_graphic(7) == false)\n"   /* BEL */
        "assert(unicode.is_graphic(127) == false)\n" /* DEL */
    );
}

/* ---- to_upper / to_lower / to_title ---- */
static void test_case_conversion(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.to_upper(\"a\") == \"A\")\n"
        "assert(unicode.to_upper(\"z\") == \"Z\")\n"
        "assert(unicode.to_upper(\"A\") == \"A\")\n"
        "assert(unicode.to_upper(97) == \"A\")\n"
        "assert(unicode.to_lower(\"A\") == \"a\")\n"
        "assert(unicode.to_lower(\"Z\") == \"z\")\n"
        "assert(unicode.to_lower(65) == \"a\")\n"
        "assert(unicode.to_title(\"a\") == \"A\")\n"
    );
}

/* ---- ord / chr ---- */
static void test_ord_chr(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.ord(\"A\") == 65)\n"
        "assert(unicode.ord(\"a\") == 97)\n"
        "assert(unicode.ord(\"0\") == 48)\n"
        "assert(unicode.chr(65) == \"A\")\n"
        "assert(unicode.chr(97) == \"a\")\n"
        "assert(unicode.chr(48) == \"0\")\n"
    );
    /* round-trip */
    RUN_OK(
        "import \"unicode\"\n"
        "var cp = unicode.ord(\"Z\")\n"
        "assert(unicode.chr(cp) == \"Z\")\n"
    );
}

/* ---- rune_count ---- */
static void test_rune_count(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.rune_count(\"\") == 0)\n"
        "assert(unicode.rune_count(\"hello\") == 5)\n"
        "assert(unicode.rune_count(\"abc\") == 3)\n"
    );
}

/* ---- rune_at ---- */
static void test_rune_at(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.rune_at(\"hello\", 0) == \"h\")\n"
        "assert(unicode.rune_at(\"hello\", 4) == \"o\")\n"
        "assert(unicode.rune_at(\"abc\", 1) == \"b\")\n"
    );
    RUN_ERR(
        "import \"unicode\"\n"
        "unicode.rune_at(\"hello\", 10)\n"
    );
}

/* ---- runes ---- */
static void test_runes(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "var r = unicode.runes(\"abc\")\n"
        "assert(r.len() == 3)\n"
        "assert(r[0] == \"a\")\n"
        "assert(r[1] == \"b\")\n"
        "assert(r[2] == \"c\")\n"
    );
    RUN_OK(
        "import \"unicode\"\n"
        "var r = unicode.runes(\"\")\n"
        "assert(r.len() == 0)\n"
    );
}

/* ---- encode_utf8 ---- */
static void test_encode_utf8(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "var b = unicode.encode_utf8(65)\n"  /* 'A' → 0x41 */
        "assert(b.len() == 1)\n"
        "assert(b.get(0) == 65)\n"
    );
    RUN_OK(
        "import \"unicode\"\n"
        "var b = unicode.encode_utf8(0x20AC)\n"  /* Euro sign → 3 bytes */
        "assert(b.len() == 3)\n"
        "assert(b.get(0) == 0xE2)\n"
        "assert(b.get(1) == 0x82)\n"
        "assert(b.get(2) == 0xAC)\n"
    );
}

/* ---- decode_utf8 ---- */
static void test_decode_utf8(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "var r = unicode.decode_utf8(\"hello\")\n"
        "assert(r[0] == \"h\")\n"
        "assert(r[1] == 1)\n"
    );
    RUN_OK(
        "import \"unicode\"\n"
        "var r = unicode.decode_utf8(\"hello\", 1)\n"
        "assert(r[0] == \"e\")\n"
        "assert(r[1] == 1)\n"
    );
}

/* ---- valid_utf8 ---- */
static void test_valid_utf8(void) {
    RUN_OK(
        "import \"unicode\"\n"
        "assert(unicode.valid_utf8(\"\") == true)\n"
        "assert(unicode.valid_utf8(\"hello\") == true)\n"
        "assert(unicode.valid_utf8(\"abc123\") == true)\n"
    );
    /* Invalid UTF-8: lone continuation byte passed as buffer */
    RUN_OK(
        "import \"unicode\"\n"
        "import \"bytes\"\n"
        "var b = bytes.from_list([0x80])\n"  /* lone continuation byte */
        "assert(unicode.valid_utf8(b) == false)\n"
    );
}

/* ---- error cases ---- */
static void test_errors(void) {
    RUN_ERR(
        "import \"unicode\"\n"
        "unicode.ord(\"\")\n"
    );
    RUN_ERR(
        "import \"unicode\"\n"
        "unicode.chr(-1)\n"
    );
    RUN_ERR(
        "import \"unicode\"\n"
        "unicode.chr(0x200000)\n"
    );
    RUN_ERR(
        "import \"unicode\"\n"
        "unicode.is_letter(3.14)\n"
    );
}

int main(void) {
    test_is_letter();
    test_is_digit();
    test_is_space();
    test_case_predicates();
    test_is_punct();
    test_is_graphic();
    test_case_conversion();
    test_ord_chr();
    test_rune_count();
    test_rune_at();
    test_runes();
    test_encode_utf8();
    test_decode_utf8();
    test_valid_utf8();
    test_errors();
    printf("test_stdlib_unicode: all tests passed\n");
    return 0;
}
