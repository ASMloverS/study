/* tests/unit/test_stdlib_hash.c - STDLIB-06: hash module */
#include "../test_assert.h"
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

#define RUN_OK(src)  TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK)
#define RUN_ERR(src) TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_RUNTIME_ERROR)

/* ---- sha256 one-shot ---- */

static void test_sha256_hello(void) {
    /* sha256("hello") = 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 */
    RUN_OK(
        "import \"hash\"\n"
        "import \"buffer\"\n"
        "var d = hash.sha256(\"hello\")\n"
        "assert(d.len() == 32)\n"
        "assert(d.to_hex() == \"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\")"
    );
}

static void test_sha256_empty(void) {
    /* sha256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    RUN_OK(
        "import \"hash\"\n"
        "var d = hash.sha256(\"\")\n"
        "assert(d.len() == 32)\n"
        "assert(d.to_hex() == \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\")"
    );
}

static void test_sha256_buffer_input(void) {
    RUN_OK(
        "import \"hash\"\n"
        "import \"buffer\"\n"
        "var b = buffer.from_str(\"hello\")\n"
        "var d = hash.sha256(b)\n"
        "assert(d.len() == 32)\n"
        "assert(d.to_hex() == \"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\")"
    );
}

/* ---- sha1 one-shot ---- */

static void test_sha1_hello(void) {
    /* sha1("hello") = aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d */
    RUN_OK(
        "import \"hash\"\n"
        "var d = hash.sha1(\"hello\")\n"
        "assert(d.len() == 20)\n"
        "assert(d.to_hex() == \"aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d\")"
    );
}

/* ---- md5 one-shot ---- */

static void test_md5_hello(void) {
    /* md5("hello") = 5d41402abc4b2a76b9719d911017c592 */
    RUN_OK(
        "import \"hash\"\n"
        "var d = hash.md5(\"hello\")\n"
        "assert(d.len() == 16)\n"
        "assert(d.to_hex() == \"5d41402abc4b2a76b9719d911017c592\")"
    );
}

static void test_md5_empty(void) {
    /* md5("") = d41d8cd98f00b204e9800998ecf8427e */
    RUN_OK(
        "import \"hash\"\n"
        "var d = hash.md5(\"\")\n"
        "assert(d.to_hex() == \"d41d8cd98f00b204e9800998ecf8427e\")"
    );
}

/* ---- crc32 ---- */

static void test_crc32_hello(void) {
    /* crc32("hello") = 0x3610a686 */
    RUN_OK(
        "import \"hash\"\n"
        "assert(hash.crc32(\"hello\") == 0x3610a686)"
    );
}

static void test_crc32_empty(void) {
    /* crc32("") = 0x00000000 */
    RUN_OK(
        "import \"hash\"\n"
        "assert(hash.crc32(\"\") == 0)"
    );
}

/* ---- fnv1a ---- */

static void test_fnv1a_64_default(void) {
    /* fnv1a-64("hello") = 0xa430d84680aabd0b */
    /* fnv1a-64("hello") = 0xa430d84680aabd0b = -6615550055289275125 (signed i64) */
    RUN_OK(
        "import \"hash\"\n"
        "var v = hash.fnv1a(\"hello\")\n"
        "assert(v == -6615550055289275125)"
    );
}

static void test_fnv1a_32(void) {
    /* fnv1a-32("hello") = 0x4f9f2cab */
    RUN_OK(
        "import \"hash\"\n"
        "assert(hash.fnv1a(\"hello\", 32) == 0x4f9f2cab)"
    );
}

/* ---- streaming Hasher (new/update/digest/reset/hexdigest) ---- */

static void test_hasher_md5_streaming(void) {
    RUN_OK(
        "import \"hash\"\n"
        "var h = hash.new(\"md5\")\n"
        "hash.update(h, \"hel\")\n"
        "hash.update(h, \"lo\")\n"
        "assert(hash.hexdigest(h) == \"5d41402abc4b2a76b9719d911017c592\")"
    );
}

static void test_hasher_sha256_streaming(void) {
    RUN_OK(
        "import \"hash\"\n"
        "var h = hash.new(\"sha256\")\n"
        "hash.update(h, \"hel\")\n"
        "hash.update(h, \"lo\")\n"
        "var d = hash.digest(h)\n"
        "assert(d.len() == 32)\n"
        "assert(d.to_hex() == \"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\")"
    );
}

static void test_hasher_reset_and_reuse(void) {
    RUN_OK(
        "import \"hash\"\n"
        "var h = hash.new(\"md5\")\n"
        "hash.update(h, \"hello\")\n"
        "var _ = hash.hexdigest(h)\n"
        "hash.reset(h)\n"
        "hash.update(h, \"hello\")\n"
        "assert(hash.hexdigest(h) == \"5d41402abc4b2a76b9719d911017c592\")"
    );
}

static void test_hasher_update_after_finalize_errors(void) {
    RUN_ERR(
        "import \"hash\"\n"
        "var h = hash.new(\"sha1\")\n"
        "hash.update(h, \"x\")\n"
        "hash.digest(h)\n"
        "hash.update(h, \"y\")"  /* should error: already finalized */
    );
}

static void test_hasher_unknown_algo_errors(void) {
    RUN_ERR(
        "import \"hash\"\n"
        "hash.new(\"sha512\")"
    );
}

static void test_hasher_sha1_streaming(void) {
    RUN_OK(
        "import \"hash\"\n"
        "var h = hash.new(\"sha1\")\n"
        "hash.update(h, \"hello\")\n"
        "assert(hash.hexdigest(h) == \"aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d\")"
    );
}

/* ---- type errors ---- */

static void test_sha256_bad_arg(void) {
    RUN_ERR(
        "import \"hash\"\n"
        "hash.sha256(42)"
    );
}

static void test_crc32_bad_arg(void) {
    RUN_ERR(
        "import \"hash\"\n"
        "hash.crc32(nil)"
    );
}

/* ---- main ---- */

int main(void) {
    test_sha256_hello();
    test_sha256_empty();
    test_sha256_buffer_input();
    test_sha1_hello();
    test_md5_hello();
    test_md5_empty();
    test_crc32_hello();
    test_crc32_empty();
    test_fnv1a_64_default();
    test_fnv1a_32();
    test_hasher_md5_streaming();
    test_hasher_sha256_streaming();
    test_hasher_reset_and_reuse();
    test_hasher_update_after_finalize_errors();
    test_hasher_unknown_algo_errors();
    test_hasher_sha1_streaming();
    test_sha256_bad_arg();
    test_crc32_bad_arg();
    printf("All hash tests passed.\n");
    return 0;
}
