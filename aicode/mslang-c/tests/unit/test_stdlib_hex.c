/* tests/unit/test_stdlib_hex.c - STDLIB-26: hex module */
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
        fprintf(stderr, "FAIL test_stdlib_hex.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_hex.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- encode lowercase ---- */
static void test_encode(void) {
    RUN_OK(
        "import \"hex\"\n"
        "assert(hex.encode(\"Hello\") == \"48656c6c6f\")\n"
        "assert(hex.encode(\"\") == \"\")\n"
        "assert(hex.encode(\"A\") == \"41\")\n"
    );
}

/* ---- encode uppercase ---- */
static void test_encode_upper(void) {
    RUN_OK(
        "import \"hex\"\n"
        "assert(hex.encode_upper(\"Hello\") == \"48656C6C6F\")\n"
        "assert(hex.encode_upper(\"\") == \"\")\n"
    );
}

/* ---- decode round-trip ---- */
static void test_decode_roundtrip(void) {
    RUN_OK(
        "import \"hex\"\n"
        "var buf = hex.decode(\"48656c6c6f\")\n"
        "assert(buf.to_str() == \"Hello\")\n"
    );
    RUN_OK(
        "import \"hex\"\n"
        "var buf = hex.decode(\"\")\n"
        "assert(buf.len() == 0)\n"
    );
    /* uppercase hex is also valid */
    RUN_OK(
        "import \"hex\"\n"
        "var buf = hex.decode(\"48656C6C6F\")\n"
        "assert(buf.to_str() == \"Hello\")\n"
    );
}

/* ---- encode→decode round-trip ---- */
static void test_roundtrip(void) {
    RUN_OK(
        "import \"hex\"\n"
        "var s = hex.encode(\"Hello, World!\")\n"
        "var buf = hex.decode(s)\n"
        "assert(buf.to_str() == \"Hello, World!\")\n"
    );
}

/* ---- buffer input to encode ---- */
static void test_buffer_input(void) {
    RUN_OK(
        "import \"hex\"\n"
        "import \"buffer\"\n"
        "var buf = buffer.from_str(\"Hi\")\n"
        "assert(hex.encode(buf) == \"4869\")\n"
        "assert(hex.encode_upper(buf) == \"4869\")\n"
    );
}

/* ---- is_valid ---- */
static void test_is_valid(void) {
    RUN_OK(
        "import \"hex\"\n"
        "assert(hex.is_valid(\"48656c6c6f\") == true)\n"
        "assert(hex.is_valid(\"\") == true)\n"
        "assert(hex.is_valid(\"FF\") == true)\n"
        "assert(hex.is_valid(\"48656g6c6f\") == false)\n"
        "assert(hex.is_valid(\"abc\") == false)\n"
    );
}

/* ---- byte_to_hex ---- */
static void test_byte_to_hex(void) {
    RUN_OK(
        "import \"hex\"\n"
        "assert(hex.byte_to_hex(255) == \"ff\")\n"
        "assert(hex.byte_to_hex(0) == \"00\")\n"
        "assert(hex.byte_to_hex(16) == \"10\")\n"
        "assert(hex.byte_to_hex(171) == \"ab\")\n"
    );
}

/* ---- hex_to_byte ---- */
static void test_hex_to_byte(void) {
    RUN_OK(
        "import \"hex\"\n"
        "assert(hex.hex_to_byte(\"ff\") == 255)\n"
        "assert(hex.hex_to_byte(\"00\") == 0)\n"
        "assert(hex.hex_to_byte(\"FF\") == 255)\n"
        "assert(hex.hex_to_byte(\"10\") == 16)\n"
        "assert(hex.hex_to_byte(\"ab\") == 171)\n"
    );
}

/* ---- decode errors ---- */
static void test_decode_errors(void) {
    /* odd-length */
    RUN_ERR(
        "import \"hex\"\n"
        "hex.decode(\"abc\")\n"
    );
    /* invalid character */
    RUN_ERR(
        "import \"hex\"\n"
        "hex.decode(\"48zz\")\n"
    );
}

/* ---- hex_to_byte errors ---- */
static void test_hex_to_byte_errors(void) {
    /* wrong length */
    RUN_ERR(
        "import \"hex\"\n"
        "hex.hex_to_byte(\"abc\")\n"
    );
    /* invalid char */
    RUN_ERR(
        "import \"hex\"\n"
        "hex.hex_to_byte(\"gg\")\n"
    );
}

/* ---- byte_to_hex range error ---- */
static void test_byte_to_hex_error(void) {
    RUN_ERR(
        "import \"hex\"\n"
        "hex.byte_to_hex(256)\n"
    );
    RUN_ERR(
        "import \"hex\"\n"
        "hex.byte_to_hex(-1)\n"
    );
}

int main(void) {
    test_encode();
    test_encode_upper();
    test_decode_roundtrip();
    test_roundtrip();
    test_buffer_input();
    test_is_valid();
    test_byte_to_hex();
    test_hex_to_byte();
    test_decode_errors();
    test_hex_to_byte_errors();
    test_byte_to_hex_error();
    printf("test_stdlib_hex: all passed\n");
    return 0;
}
