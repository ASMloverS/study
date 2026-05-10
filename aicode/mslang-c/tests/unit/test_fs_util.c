#include "test_assert.h"
#include "ms/fs_util.h"
#include <stdio.h>

#ifdef _WIN32
#  include <direct.h>
#  define ms_rmdir _rmdir
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  define ms_rmdir rmdir
#endif

static FILE* open_tmp(const char* path) {
    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, path, "wb");
#else
    f = fopen(path, "wb");
#endif
    return f;
}

static void test_stat_existing(void) {
    FILE* f = open_tmp("_t01_src.tmp");
    TEST_ASSERT(f != NULL);
    fwrite("hello", 1, 5, f);
    fclose(f);

    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("_t01_src.tmp", &m));
    TEST_ASSERT_EQ((int)m.size, 5);
    TEST_ASSERT(m.mtime_ns != 0);
    remove("_t01_src.tmp");
}

static void test_stat_missing(void) {
    MsFileMeta m;
    TEST_ASSERT(!ms_fs_stat("_t01_no_such_file.tmp", &m));
}

static void test_mkdir_creates_and_eexist_ok(void) {
    ms_rmdir("_t01_testdir");
    TEST_ASSERT(ms_fs_mkdir("_t01_testdir"));
    TEST_ASSERT(ms_fs_mkdir("_t01_testdir"));  /* EEXIST => still true */
    ms_rmdir("_t01_testdir");
}

static void test_atomic_rename(void) {
    FILE* f = open_tmp("_t01_rename_src.tmp");
    TEST_ASSERT(f != NULL);
    fwrite("data", 1, 4, f);
    fclose(f);

    TEST_ASSERT(ms_fs_atomic_rename("_t01_rename_src.tmp", "_t01_rename_dst.tmp"));

    MsFileMeta m;
    TEST_ASSERT(!ms_fs_stat("_t01_rename_src.tmp", &m));
    TEST_ASSERT(ms_fs_stat("_t01_rename_dst.tmp", &m));
    TEST_ASSERT_EQ((int)m.size, 4);
    remove("_t01_rename_dst.tmp");
}

static void test_atomic_rename_replaces_existing(void) {
    FILE* f = open_tmp("_t01_old.tmp");
    TEST_ASSERT(f != NULL);
    fwrite("old", 1, 3, f);
    fclose(f);

    f = open_tmp("_t01_new.tmp");
    TEST_ASSERT(f != NULL);
    fwrite("new!", 1, 4, f);
    fclose(f);

    TEST_ASSERT(ms_fs_atomic_rename("_t01_new.tmp", "_t01_old.tmp"));

    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("_t01_old.tmp", &m));
    TEST_ASSERT_EQ((int)m.size, 4);
    remove("_t01_old.tmp");
}

static void test_unlink(void) {
    FILE* f = open_tmp("_t01_unlink.tmp");
    TEST_ASSERT(f != NULL);
    fclose(f);
    TEST_ASSERT(ms_fs_unlink("_t01_unlink.tmp"));
    MsFileMeta m;
    TEST_ASSERT(!ms_fs_stat("_t01_unlink.tmp", &m));
}

int main(void) {
    test_stat_existing();
    test_stat_missing();
    test_mkdir_creates_and_eexist_ok();
    test_atomic_rename();
    test_atomic_rename_replaces_existing();
    test_unlink();
    printf("test_fs_util: all passed\n");
    return 0;
}
