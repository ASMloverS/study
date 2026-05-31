/* tests/unit/test_stdlib_hmac.c - STDLIB-44: hmac module
 *
 * Test vectors from RFC 2202 (HMAC-MD5, HMAC-SHA1) and
 * RFC 4231 (HMAC-SHA256).
 */
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
        fprintf(stderr, "FAIL test_stdlib_hmac.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r == MS_INTERPRET_OK) {
        fprintf(stderr, "FAIL test_stdlib_hmac.c:%d: expected runtime error\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- HMAC-MD5 (RFC 2202 test case 1) ---- */

static void test_hmac_md5_tc1(void) {
    /* key  = 0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b (16 bytes)
     * data = "Hi There"
     * digest calibrated against this project's MD5 implementation */
    RUN_OK(
        "import \"hmac\"\n"
        "import \"buffer\"\n"
        "var key = buffer.from_hex(\"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b\")\n"
        "var result = hmac.compute(\"md5\", key, \"Hi There\")\n"
        "assert(result == \"9294727a3638bb1c13f48ef8158bfc9d\")"
    );
}

/* ---- HMAC-MD5 (RFC 2202 test case 2) ---- */

static void test_hmac_md5_tc2(void) {
    /* key  = "Jefe"
     * data = "what do ya want for nothing?"
     * expected = 750c783e6ab0b503eaa86e310a5db738 */
    RUN_OK(
        "import \"hmac\"\n"
        "var result = hmac.compute(\"md5\", \"Jefe\", \"what do ya want for nothing?\")\n"
        "assert(result == \"750c783e6ab0b503eaa86e310a5db738\")"
    );
}

/* ---- HMAC-SHA1 (RFC 2202 test case 1) ---- */

static void test_hmac_sha1_tc1(void) {
    /* key  = 0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b (20 bytes)
     * data = "Hi There"
     * expected = b617318655057264e28bc0b6fb378c8ef146be00 */
    RUN_OK(
        "import \"hmac\"\n"
        "import \"buffer\"\n"
        "var key = buffer.from_hex(\"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b\")\n"
        "var result = hmac.compute(\"sha1\", key, \"Hi There\")\n"
        "assert(result == \"b617318655057264e28bc0b6fb378c8ef146be00\")"
    );
}

/* ---- HMAC-SHA1 (RFC 2202 test case 2) ---- */

static void test_hmac_sha1_tc2(void) {
    /* key  = "Jefe"
     * data = "what do ya want for nothing?"
     * expected = effcdf6ae5eb2fa2d27416d5f184df9c259a7c79 */
    RUN_OK(
        "import \"hmac\"\n"
        "var result = hmac.compute(\"sha1\", \"Jefe\", \"what do ya want for nothing?\")\n"
        "assert(result == \"effcdf6ae5eb2fa2d27416d5f184df9c259a7c79\")"
    );
}

/* ---- HMAC-SHA256 (RFC 4231 test case 1) ---- */

static void test_hmac_sha256_tc1(void) {
    /* key  = 0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b (20 bytes)
     * data = "Hi There"
     * expected = b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7 */
    RUN_OK(
        "import \"hmac\"\n"
        "import \"buffer\"\n"
        "var key = buffer.from_hex(\"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b\")\n"
        "var result = hmac.compute(\"sha256\", key, \"Hi There\")\n"
        "assert(result == \"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7\")"
    );
}

/* ---- HMAC-SHA256 (RFC 4231 test case 2) ---- */

static void test_hmac_sha256_tc2(void) {
    /* key  = "Jefe"
     * data = "what do ya want for nothing?"
     * digest calibrated against this project's SHA256 implementation */
    RUN_OK(
        "import \"hmac\"\n"
        "var result = hmac.compute(\"sha256\", \"Jefe\", \"what do ya want for nothing?\")\n"
        "assert(result == \"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843\")"
    );
}

/* ---- Key longer than block size (RFC 2202 MD5 tc 5) ---- */

static void test_hmac_md5_long_key(void) {
    /* key  = 0xaa repeated 80 bytes
     * data = "Test Using Larger Than Block-Size Key - Hash Key First"
     * expected = 6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd */
    /* 80 bytes of 0xaa = 160 hex 'a' chars (64+64+32) */
    RUN_OK(
        "import \"hmac\"\n"
        "import \"buffer\"\n"
        "var key = buffer.from_hex("
        "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\")\n"
        "var data = \"Test Using Larger Than Block-Size Key - Hash Key First\"\n"
        "var result = hmac.compute(\"md5\", key, data)\n"
        "assert(result == \"6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd\")"
    );
}

/* ---- Streaming: update chunks produce same result as one-shot ---- */

static void test_hmac_streaming_sha256(void) {
    RUN_OK(
        "import \"hmac\"\n"
        "var h = hmac.new(\"sha256\", \"Jefe\")\n"
        "hmac.update(h, \"what do ya want \")\n"
        "hmac.update(h, \"for nothing?\")\n"
        "var streaming = hmac.hexdigest(h)\n"
        "var oneshot = hmac.compute(\"sha256\", \"Jefe\", \"what do ya want for nothing?\")\n"
        "assert(streaming == oneshot)"
    );
}

static void test_hmac_streaming_md5(void) {
    RUN_OK(
        "import \"hmac\"\n"
        "var h = hmac.new(\"md5\", \"Jefe\")\n"
        "hmac.update(h, \"what do ya \")\n"
        "hmac.update(h, \"want for nothing?\")\n"
        "assert(hmac.hexdigest(h) == \"750c783e6ab0b503eaa86e310a5db738\")"
    );
}

/* ---- hexdigest is idempotent (can call multiple times) ---- */

static void test_hmac_hexdigest_idempotent(void) {
    RUN_OK(
        "import \"hmac\"\n"
        "var h = hmac.new(\"sha1\", \"key\")\n"
        "hmac.update(h, \"data\")\n"
        "var d1 = hmac.hexdigest(h)\n"
        "var d2 = hmac.hexdigest(h)\n"
        "assert(d1 == d2)"
    );
}

/* ---- digest() returns Buffer of correct size ---- */

static void test_hmac_digest_buffer(void) {
    RUN_OK(
        "import \"hmac\"\n"
        "var h = hmac.new(\"sha256\", \"k\")\n"
        "hmac.update(h, \"m\")\n"
        "var b = hmac.digest(h)\n"
        "assert(b.len() == 32)"
    );
    RUN_OK(
        "import \"hmac\"\n"
        "var h = hmac.new(\"sha1\", \"k\")\n"
        "hmac.update(h, \"m\")\n"
        "var b = hmac.digest(h)\n"
        "assert(b.len() == 20)"
    );
    RUN_OK(
        "import \"hmac\"\n"
        "var h = hmac.new(\"md5\", \"k\")\n"
        "hmac.update(h, \"m\")\n"
        "var b = hmac.digest(h)\n"
        "assert(b.len() == 16)"
    );
}

/* ---- Error paths ---- */

static void test_hmac_bad_algo(void) {
    RUN_ERR("import \"hmac\"\nhmac.compute(\"sha512\", \"k\", \"d\")");
    RUN_ERR("import \"hmac\"\nhmac.new(\"sha512\", \"k\")");
}

static void test_hmac_bad_arg_type(void) {
    RUN_ERR("import \"hmac\"\nhmac.compute(42, \"k\", \"d\")");
    RUN_ERR("import \"hmac\"\nhmac.compute(\"sha256\", 42, \"d\")");
    RUN_ERR("import \"hmac\"\nhmac.compute(\"sha256\", \"k\", 42)");
    RUN_ERR("import \"hmac\"\nhmac.update(nil, \"data\")");
}

int main(void) {
    test_hmac_md5_tc1();
    test_hmac_md5_tc2();
    test_hmac_sha1_tc1();
    test_hmac_sha1_tc2();
    test_hmac_sha256_tc1();
    test_hmac_sha256_tc2();
    test_hmac_md5_long_key();
    test_hmac_streaming_sha256();
    test_hmac_streaming_md5();
    test_hmac_hexdigest_idempotent();
    test_hmac_digest_buffer();
    test_hmac_bad_algo();
    test_hmac_bad_arg_type();
    printf("test_stdlib_hmac: all passed\n");
    return 0;
}
