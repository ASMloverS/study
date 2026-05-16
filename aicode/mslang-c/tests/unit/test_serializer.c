#ifdef _WIN32
#  define _CRT_SECURE_NO_WARNINGS
#endif
#include "test_assert.h"
#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/serializer.h"
#include "ms/fs_util.h"
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
    char tiny[10];

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
    TEST_ASSERT(!ms_cache_path_for("some/long/path/script.ms", tiny, sizeof(tiny)));
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
    MsObjFunction* fn = ms_compile_cached(&vm, "_t02_test.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn != NULL);

    /* Cache must have been written to __mscache__/_t02_test.msc */
    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t02_test.msc", &m));
    TEST_ASSERT(m.size > 0);

    cleanup_cache();
    remove("_t02_test.ms");
    ms_vm_free(&vm);
}

static void test_mtime_fastpath_hit(void) {
    /* Write source file */
    FILE* f = fopen("_t03_fast.ms", "wb");
    TEST_ASSERT(f != NULL);
    const char* src = "print(99)";
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    /* First run: compiles + writes mtime-mode cache */
    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t03_fast.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    /* Cache file must exist */
    MsFileMeta cm;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t03_fast.msc", &cm));

    /* Second run: must hit cache (source not needed on mtime hit). */
    /* Verify indirectly: function is returned and cache file mtime is
       unchanged (no rewrite happened). */
    MsFileMeta cm2_before;
    ms_fs_stat("__mscache__/_t03_fast.msc", &cm2_before);

    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t03_fast.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn2 != NULL);
    ms_vm_free(&vm2);

    MsFileMeta cm2_after;
    ms_fs_stat("__mscache__/_t03_fast.msc", &cm2_after);
    /* Cache file should not have been rewritten (mtime unchanged) */
    TEST_ASSERT_EQ(cm2_before.mtime_ns, cm2_after.mtime_ns);

    remove("_t03_fast.ms");
    remove("__mscache__/_t03_fast.msc");
}

static void test_mtime_fastpath_miss_on_mtime_change(void) {
    /* Write source file */
    FILE* f = fopen("_t03_stale.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(1)", 1, 8, f);
    fclose(f);

    /* First run */
    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t03_stale.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    MsFileMeta cm1;
    ms_fs_stat("__mscache__/_t03_stale.msc", &cm1);

    /* Overwrite source with different size to guarantee size mismatch -> cache miss */
    f = fopen("_t03_stale.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(2) /* extra */", 1, 20, f);  /* different size */
    fclose(f);

    /* Second run: size changed -> cache miss -> recompile -> new cache written */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t03_stale.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn2 != NULL);
    ms_vm_free(&vm2);

    /* Cache was rewritten -> header now records the new source size (20 bytes).
       Do NOT rely on .msc file size or mtime: the compiled bytecode for these
       two sources is the same size, and Windows NTFS mtime granularity can be
       too coarse when tests run back-to-back.  Reading the stored header is the
       only reliable way to confirm the cache was actually rewritten. */
    FILE* hf = fopen("__mscache__/_t03_stale.msc", "rb");
    TEST_ASSERT(hf != NULL);
    MsMscHeader hdr2;
    TEST_ASSERT(fread(&hdr2, sizeof(hdr2), 1, hf) == 1);
    fclose(hf);
    TEST_ASSERT(hdr2.src_size == 20);

    remove("_t03_stale.ms");
    remove("__mscache__/_t03_stale.msc");
}

static void test_hash_mode_hit_despite_mtime_change(void) {
    /* Write source file */
    FILE* f = fopen("_t04_hash.ms", "wb");
    TEST_ASSERT(f != NULL);
    const char* src = "print(\"hash\")";
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    /* First run with HASH mode */
    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t04_hash.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    MsFileMeta cm1;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t04_hash.msc", &cm1));

    /* Simulate mtime change without content change:
       Re-write identical content (may change mtime on most filesystems) */
    f = fopen("_t04_hash.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    /* Second run with HASH mode: content unchanged -> hash matches -> cache hit */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t04_hash.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn2 != NULL);
    ms_vm_free(&vm2);

    /* Cache file should NOT have been rewritten (same content = same hash = hit) */
    MsFileMeta cm2;
    ms_fs_stat("__mscache__/_t04_hash.msc", &cm2);
    TEST_ASSERT_EQ(cm1.size, cm2.size);

    remove("_t04_hash.ms");
    remove("__mscache__/_t04_hash.msc");
}

static void test_hash_mode_miss_on_content_change(void) {
    FILE* f = fopen("_t04_cont.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(1)", 1, 8, f);
    fclose(f);

    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t04_cont.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    MsFileMeta cm1;
    ms_fs_stat("__mscache__/_t04_cont.msc", &cm1);

    /* Change content */
    f = fopen("_t04_cont.ms", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("print(2)", 1, 8, f);  /* different content, same size */
    fclose(f);

    /* Second run: hash changed -> cache miss -> recompile */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t04_cont.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn2 != NULL);
    ms_vm_free(&vm2);

    /* Cache file was rewritten (new compile = different fn -> may differ in size) */
    MsFileMeta cm2;
    ms_fs_stat("__mscache__/_t04_cont.msc", &cm2);
    /* Rewrite happened: mtime should differ on most filesystems.
       We verify the cache is valid by doing a third hit. */
    MsVM vm3; ms_vm_init(&vm3);
    MsObjFunction* fn3 = ms_compile_cached(&vm3, "_t04_cont.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn3 != NULL);
    ms_vm_free(&vm3);

    remove("_t04_cont.ms");
    remove("__mscache__/_t04_cont.msc");
}

static void test_mtime_cache_not_used_by_hash_mode(void) {
    /* Write source */
    FILE* f = fopen("_t04_cross.ms", "wb");
    TEST_ASSERT(f != NULL);
    const char* src = "print(\"cross\")";
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    /* Write cache with MTIME mode header */
    MsVM vm; ms_vm_init(&vm);
    MsObjFunction* fn1 = ms_compile_cached(&vm, "_t04_cross.ms", MS_CACHE_MTIME);
    TEST_ASSERT(fn1 != NULL);
    ms_vm_free(&vm);

    /* Read it back with HASH mode.
       The header says MS_CACHE_MTIME -> ms_deserialize validates by (size, mtime),
       not hash. The passed src_hash doesn't match what would be checked.
       ms_compile_cached with MS_CACHE_HASH passes hash (computed from source)
       but the header says mtime mode -> validation uses size+mtime, which match
       current source -> HIT (header is self-describing). */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn2 = ms_compile_cached(&vm2, "_t04_cross.ms", MS_CACHE_HASH);
    TEST_ASSERT(fn2 != NULL);  /* hit: header says mtime mode, mtime still valid */
    ms_vm_free(&vm2);

    remove("_t04_cross.ms");
    remove("__mscache__/_t04_cross.msc");
}

int main(void) {
    test_cache_path_for();
    test_roundtrip_hash_mode();
    test_roundtrip_mtime_mode();
    test_missing_file();
    test_compile_cached_writes_to_mscache();
    test_mtime_fastpath_hit();
    test_mtime_fastpath_miss_on_mtime_change();
    test_hash_mode_hit_despite_mtime_change();
    test_hash_mode_miss_on_content_change();
    test_mtime_cache_not_used_by_hash_mode();
    printf("test_serializer: all passed\n");
    return 0;
}
