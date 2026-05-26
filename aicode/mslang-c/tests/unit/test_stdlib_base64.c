/* tests/unit/test_stdlib_base64.c - STDLIB-25: base64 module */
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
        fprintf(stderr, "FAIL test_stdlib_base64.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_base64.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- encode/decode round-trip ---- */
static void test_roundtrip(void) {
    RUN_OK(
        "import \"base64\"\n"
        "var s = base64.encode(\"Hello, World!\")\n"
        "assert(s == \"SGVsbG8sIFdvcmxkIQ==\")\n"
        "var buf = base64.decode(s)\n"
        "assert(buf.to_str() == \"Hello, World!\")\n"
    );
}

/* ---- empty string ---- */
static void test_empty(void) {
    RUN_OK(
        "import \"base64\"\n"
        "var s = base64.encode(\"\")\n"
        "assert(s == \"\")\n"
        "var buf = base64.decode(\"\")\n"
        "assert(buf.len() == 0)\n"
    );
}

/* ---- padding correctness (lengths not multiple of 3) ---- */
static void test_padding(void) {
    /* 1 byte -> 2 chars + "==" */
    RUN_OK(
        "import \"base64\"\n"
        "var s = base64.encode(\"A\")\n"
        "assert(s == \"QQ==\")\n"
    );
    /* 2 bytes -> 3 chars + "=" */
    RUN_OK(
        "import \"base64\"\n"
        "var s = base64.encode(\"AB\")\n"
        "assert(s == \"QUI=\")\n"
    );
    /* 3 bytes -> 4 chars, no padding */
    RUN_OK(
        "import \"base64\"\n"
        "var s = base64.encode(\"ABC\")\n"
        "assert(s == \"QUJD\")\n"
    );
}

/* ---- URL-safe encoding round-trip ---- */
static void test_url_safe(void) {
    RUN_OK(
        "import \"base64\"\n"
        "var enc = base64.encode_url(\"Hello, World!\")\n"
        "var buf = base64.decode_url(enc)\n"
        "assert(buf.to_str() == \"Hello, World!\")\n"
    );
    /* URL-safe output must not contain +, /, or = */
    RUN_OK(
        "import \"base64\"\n"
        "var enc = base64.encode_url(\"Man\")\n"
        /* standard would be \"TWFu\", URL-safe same (no special chars) */
        "var buf = base64.decode_url(enc)\n"
        "assert(buf.to_str() == \"Man\")\n"
    );
}

/* ---- encode_raw / decode_raw (no padding) ---- */
static void test_raw(void) {
    RUN_OK(
        "import \"base64\"\n"
        "var s = base64.encode_raw(\"A\")\n"
        "assert(s == \"QQ\")\n"
        "var s2 = base64.encode_raw(\"AB\")\n"
        "assert(s2 == \"QUI\")\n"
        "var buf = base64.decode_raw(\"QUI\")\n"
        "assert(buf.to_str() == \"AB\")\n"
    );
    /* round-trip for 3-byte input (no padding difference) */
    RUN_OK(
        "import \"base64\"\n"
        "var s = base64.encode_raw(\"ABC\")\n"
        "assert(s == \"QUJD\")\n"
        "var buf = base64.decode_raw(s)\n"
        "assert(buf.to_str() == \"ABC\")\n"
    );
}

/* ---- is_valid ---- */
static void test_is_valid(void) {
    RUN_OK(
        "import \"base64\"\n"
        "assert(base64.is_valid(\"SGVs\") == true)\n"
        "assert(base64.is_valid(\"SGVsbG8sIFdvcmxkIQ==\") == true)\n"
        "assert(base64.is_valid(\"\") == true)\n"
        "assert(base64.is_valid(\"SGVs$\") == false)\n"
        "assert(base64.is_valid(\"SGVs!\") == false)\n"
    );
}

/* ---- encoded_len / decoded_len ---- */
static void test_len_helpers(void) {
    RUN_OK(
        "import \"base64\"\n"
        /* 0 bytes -> 0 encoded chars */
        "assert(base64.encoded_len(0) == 0)\n"
        /* 3 bytes -> 4 chars */
        "assert(base64.encoded_len(3) == 4)\n"
        /* 1 byte -> 4 chars (with padding) */
        "assert(base64.encoded_len(1) == 4)\n"
        /* 6 bytes -> 8 chars */
        "assert(base64.encoded_len(6) == 8)\n"
    );
    RUN_OK(
        "import \"base64\"\n"
        /* \"SGVs\" (4 chars) -> at most 3 bytes */
        "assert(base64.decoded_len(\"SGVs\") == 3)\n"
        /* empty -> 0 */
        "assert(base64.decoded_len(\"\") == 0)\n"
    );
}

/* ---- invalid base64 decode raises error ---- */
static void test_decode_errors(void) {
    RUN_ERR(
        "import \"base64\"\n"
        "base64.decode(\"SGVs$\")\n"
    );
    RUN_ERR(
        "import \"base64\"\n"
        "base64.decode(\"!!!\")\n"
    );
}

/* ---- buffer input to encode ---- */
static void test_buffer_input(void) {
    RUN_OK(
        "import \"base64\"\n"
        "import \"buffer\"\n"
        "var buf = buffer.from_str(\"Hello\")\n"
        "var s = base64.encode(buf)\n"
        "assert(s == \"SGVsbG8=\")\n"
    );
}

int main(void) {
    test_roundtrip();
    test_empty();
    test_padding();
    test_url_safe();
    test_raw();
    test_is_valid();
    test_len_helpers();
    test_decode_errors();
    test_buffer_input();
    printf("test_stdlib_base64: all passed\n");
    return 0;
}
