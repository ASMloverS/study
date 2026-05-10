#include "test_assert.h"
#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/serializer.h"
#include "ms/module.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <direct.h>
#  define ms_rmdir _rmdir
#else
#  include <unistd.h>
#  define ms_rmdir rmdir
#endif

/* ---- helpers ---- */

static void cleanup_cache(void) {
    remove("__mscache__/_t02_test.msc");
    ms_rmdir("__mscache__");
    remove("_t02_test.msc");
}

/* ---- tests ---- */

static void test_cache_path_for(void) {
    char out[512];

    /* script with directory and .ms extension */
    TEST_ASSERT(ms_cache_path_for("tests/foo.ms", out, sizeof(out)));
    TEST_ASSERT_STR_EQ(out, "tests/__mscache__/foo.msc");

    /* script without directory */
    TEST_ASSERT(ms_cache_path_for("bar.ms", out, sizeof(out)));
    TEST_ASSERT_STR_EQ(out, "__mscache__/bar.msc");

    /* script without .ms extension */
    TEST_ASSERT(ms_cache_path_for("baz", out, sizeof(out)));
    TEST_ASSERT_STR_EQ(out, "__mscache__/baz.msc");

    /* buffer too small -> false */
    char small[10];
    TEST_ASSERT(!ms_cache_path_for("some/long/path/script.ms", small, sizeof(small)));
}

static void test_roundtrip_hash_mode(void) {
    cleanup_cache();

    MsVM vm; ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    const char* src = "fun add(a,b) { return a + b }\nprint(add(1,2))";
    MsObjFunction* fn = ms_compile(&vm, src, "_t02_test.ms", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);

    uint32_t hash = ms_fnv1a(src, (int)strlen(src));

    /* Build a hash-mode header */
    MsMscHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = 'M'; hdr.magic[1] = 'S'; hdr.magic[2] = 'C'; hdr.magic[3] = '\0';
    hdr.version   = MS_MSC_VERSION;
    hdr.flags     = MS_CACHE_HASH;
    hdr.src_hash  = hash;

    TEST_ASSERT(ms_serialize(fn, "_t02_test.msc", &hdr));

    /* correct hash -> hit */
    MsObjFunction* fn2 = ms_deserialize(&vm, "_t02_test.msc", 0, 0, hash);
    TEST_ASSERT(fn2 != NULL);
    TEST_ASSERT_EQ(fn2->arity, fn->arity);
    TEST_ASSERT_EQ(fn2->chunk.code_count, fn->chunk.code_count);

    /* wrong hash -> miss */
    TEST_ASSERT(ms_deserialize(&vm, "_t02_test.msc", 0, 0, hash + 1) == NULL);

    remove("_t02_test.msc");
    ms_vm_free(&vm);
}

static void test_roundtrip_mtime_mode(void) {
    cleanup_cache();

    MsVM vm; ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    const char* src = "print(42)";
    MsObjFunction* fn = ms_compile(&vm, src, "_t02_mtime.ms", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);

    MsMscHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = 'M'; hdr.magic[1] = 'S'; hdr.magic[2] = 'C'; hdr.magic[3] = '\0';
    hdr.version      = MS_MSC_VERSION;
    hdr.flags        = MS_CACHE_MTIME;
    hdr.src_size     = 9;       /* len("print(42)") */
    hdr.src_mtime_ns = 1234567890000000000LL;

    TEST_ASSERT(ms_serialize(fn, "_t02_mtime.msc", &hdr));

    /* correct size+mtime -> hit */
    MsObjFunction* fn2 = ms_deserialize(&vm, "_t02_mtime.msc",
                                         9, 1234567890000000000LL, 0);
    TEST_ASSERT(fn2 != NULL);

    /* wrong mtime -> miss */
    TEST_ASSERT(ms_deserialize(&vm, "_t02_mtime.msc",
                                9, 1234567890000000001LL, 0) == NULL);

    /* wrong size -> miss */
    TEST_ASSERT(ms_deserialize(&vm, "_t02_mtime.msc",
                                10, 1234567890000000000LL, 0) == NULL);

    remove("_t02_mtime.msc");
    ms_vm_free(&vm);
}

static void test_missing_file(void) {
    MsVM vm; ms_vm_init(&vm);
    TEST_ASSERT(ms_deserialize(&vm, "_t02_no_such_file.msc", 0, 0, 0) == NULL);
    ms_vm_free(&vm);
}

static void test_compile_cached_writes_to_mscache(void) {
    cleanup_cache();

    /* Write a minimal .ms source file so ms_compile_cached can read it */
    FILE* f = fopen("_t02_test.ms", "wb");
    TEST_ASSERT(f != NULL);
    const char* src = "print(1)";
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    MsVM vm; ms_vm_init(&vm);
    char* source = ms_read_file("_t02_test.ms");
    TEST_ASSERT(source != NULL);

    MsObjFunction* fn = ms_compile_cached(&vm, source, "_t02_test.ms");
    TEST_ASSERT(fn != NULL);
    free(source);

    /* Cache must have been written to __mscache__/_t02_test.msc */
    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t02_test.msc", &m));
    TEST_ASSERT(m.size > 0);

    cleanup_cache();
    remove("_t02_test.ms");
    ms_vm_free(&vm);
}

int main(void) {
    test_cache_path_for();
    test_roundtrip_hash_mode();
    test_roundtrip_mtime_mode();
    test_missing_file();
    test_compile_cached_writes_to_mscache();
    printf("test_serializer: all passed\n");
    return 0;
}
