/* tests/unit/test_stdlib_base32.c - STDLIB-34: base32 module */
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
        fprintf(stderr, "FAIL test_stdlib_base32.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_base32.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- encode round-trip ---- */
static void test_roundtrip(void) {
    /* RFC 4648 test vector: "Hello" → "JBSWY3DP" (no padding since 5 bytes) */
    RUN_OK(
        "import \"base32\"\n"
        "var s = base32.encode(\"Hello\")\n"
        "assert(s == \"JBSWY3DP\")\n"
        "var buf = base32.decode(s)\n"
        "assert(buf.to_str() == \"Hello\")\n"
    );
}

/* ---- empty string ---- */
static void test_empty(void) {
    RUN_OK(
        "import \"base32\"\n"
        "var s = base32.encode(\"\")\n"
        "assert(s == \"\")\n"
        "var buf = base32.decode(\"\")\n"
        "assert(buf.len() == 0)\n"
    );
}

/* ---- RFC 4648 test vectors (padding) ---- */
static void test_rfc_vectors(void) {
    /* 1 byte: 'f' → "MY======"  */
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.encode(\"f\") == \"MY======\")\n"
    );
    /* 2 bytes: 'fo' → "MZXQ====" */
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.encode(\"fo\") == \"MZXQ====\")\n"
    );
    /* 3 bytes: 'foo' → "MZXW6===" */
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.encode(\"foo\") == \"MZXW6===\")\n"
    );
    /* 4 bytes: 'foob' → "MZXW6YQ=" */
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.encode(\"foob\") == \"MZXW6YQ=\")\n"
    );
    /* 5 bytes: 'fooba' → "MZXW6YTB" (no padding) */
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.encode(\"fooba\") == \"MZXW6YTB\")\n"
    );
    /* 6 bytes: 'foobar' → "MZXW6YTBOI======" */
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.encode(\"foobar\") == \"MZXW6YTBOI======\")\n"
    );
}

/* ---- decode RFC vectors ---- */
static void test_decode_vectors(void) {
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.decode(\"MY======\").to_str() == \"f\")\n"
        "assert(base32.decode(\"MZXQ====\").to_str() == \"fo\")\n"
        "assert(base32.decode(\"MZXW6===\").to_str() == \"foo\")\n"
        "assert(base32.decode(\"MZXW6YQ=\").to_str() == \"foob\")\n"
        "assert(base32.decode(\"MZXW6YTB\").to_str() == \"fooba\")\n"
        "assert(base32.decode(\"MZXW6YTBOI======\").to_str() == \"foobar\")\n"
    );
}

/* ---- encode_hex (extended hex alphabet) ---- */
static void test_encode_hex(void) {
    /* RFC 4648 §10 test vectors for base32hex */
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.encode_hex(\"f\") == \"CO======\")\n"
        "assert(base32.encode_hex(\"fo\") == \"CPNG====\")\n"
        "assert(base32.encode_hex(\"foo\") == \"CPNMU===\")\n"
        "assert(base32.encode_hex(\"foob\") == \"CPNMUOG=\")\n"
        "assert(base32.encode_hex(\"fooba\") == \"CPNMUOJ1\")\n"
        "assert(base32.encode_hex(\"foobar\") == \"CPNMUOJ1E8======\")\n"
    );
}

/* ---- decode_hex round-trip ---- */
static void test_decode_hex(void) {
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.decode_hex(\"CO======\").to_str() == \"f\")\n"
        "assert(base32.decode_hex(\"CPNG====\").to_str() == \"fo\")\n"
        "assert(base32.decode_hex(\"CPNMU===\").to_str() == \"foo\")\n"
        "assert(base32.decode_hex(\"CPNMUOG=\").to_str() == \"foob\")\n"
        "assert(base32.decode_hex(\"CPNMUOJ1\").to_str() == \"fooba\")\n"
        "assert(base32.decode_hex(\"CPNMUOJ1E8======\").to_str() == \"foobar\")\n"
    );
}

/* ---- encode_raw (no padding) ---- */
static void test_encode_raw(void) {
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.encode_raw(\"f\") == \"MY\")\n"
        "assert(base32.encode_raw(\"fo\") == \"MZXQ\")\n"
        "assert(base32.encode_raw(\"foo\") == \"MZXW6\")\n"
        "assert(base32.encode_raw(\"foob\") == \"MZXW6YQ\")\n"
        "assert(base32.encode_raw(\"fooba\") == \"MZXW6YTB\")\n"
    );
}

/* ---- is_valid ---- */
static void test_is_valid(void) {
    RUN_OK(
        "import \"base32\"\n"
        "assert(base32.is_valid(\"JBSWY3DP\") == true)\n"
        "assert(base32.is_valid(\"MY======\") == true)\n"
        "assert(base32.is_valid(\"\") == true)\n"
        "assert(base32.is_valid(\"JBSWY3DP$\") == false)\n"
        "assert(base32.is_valid(\"JBSWY3DP!\") == false)\n"
        /* '1' is not in standard alphabet */
        "assert(base32.is_valid(\"1BSWY3DP\") == false)\n"
    );
}

/* ---- invalid decode raises runtime error ---- */
static void test_decode_errors(void) {
    RUN_ERR(
        "import \"base32\"\n"
        "base32.decode(\"JBSWY3DP$\")\n"
    );
    RUN_ERR(
        "import \"base32\"\n"
        "base32.decode(\"!!!\")\n"
    );
}

/* ---- buffer input to encode ---- */
static void test_buffer_input(void) {
    RUN_OK(
        "import \"base32\"\n"
        "import \"buffer\"\n"
        "var buf = buffer.from_str(\"Hello\")\n"
        "var s = base32.encode(buf)\n"
        "assert(s == \"JBSWY3DP\")\n"
    );
}

int main(void) {
    test_roundtrip();
    test_empty();
    test_rfc_vectors();
    test_decode_vectors();
    test_encode_hex();
    test_decode_hex();
    test_encode_raw();
    test_is_valid();
    test_decode_errors();
    test_buffer_input();
    printf("test_stdlib_base32: all passed\n");
    return 0;
}
