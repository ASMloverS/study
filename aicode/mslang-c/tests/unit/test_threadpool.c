/* test_threadpool.c - Unit tests for MsThreadPool (CAPI-07).
 * Tests pure threadpool mechanics: submit/pop_done without VM/GC. */
#include "test_assert.h"
#include "ms/threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define tp_strdup _strdup
#  define tp_sleep_ms(ms) Sleep(ms)
#else
#  include <time.h>
#  include <unistd.h>
#  define tp_strdup strdup
static void tp_sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

/* ---- helpers ---- */

/* Write content to a temp file, return heap-allocated path. Caller must remove+free. */
static char* make_temp_file(const char* content, size_t len) {
#ifdef _WIN32
    char* path = (char*)malloc(MAX_PATH);
    char tmp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_dir);
    GetTempFileNameA(tmp_dir, "mst", 0, path);
    FILE* f = fopen(path, "wb");
#else
    char* path = (char*)malloc(64);
    snprintf(path, 64, "/tmp/ms_tp_test_%d.bin", (int)getpid());
    FILE* f = fopen(path, "wb");
#endif
    if (f) { fwrite(content, 1, len, f); fclose(f); }
    return path;
}

/* Spin-wait (with tiny sleeps) until done_head is non-NULL, max N ms. */
static MsJob* wait_done(MsThreadPool* tp, int max_ms) {
    for (int i = 0; i < max_ms; i++) {
        MsJob* j = ms_threadpool_pop_done(tp);
        if (j) return j;
        tp_sleep_ms(1);
    }
    return NULL;
}

/* ---- test: init/destroy is safe with no jobs ---- */

static void test_init_destroy(void) {
    MsThreadPool tp;
    ms_threadpool_init(&tp, 2);
    TEST_ASSERT(tp.thread_count == 2);
    TEST_ASSERT(!tp.shutdown);
    TEST_ASSERT(tp.head == NULL);
    TEST_ASSERT(tp.done_head == NULL);
    ms_threadpool_destroy(&tp);
}

/* ---- test: read file async ---- */

static void test_read_file(void) {
    const char* content = "hello threadpool";
    size_t clen = strlen(content);
    char* path = make_temp_file(content, clen);

    MsThreadPool tp;
    ms_threadpool_init(&tp, 2);

    MsJob* job = (MsJob*)calloc(1, sizeof(MsJob));
    job->kind          = MS_JOB_READ_FILE;
    job->path          = tp_strdup(path);
    job->future_opaque = NULL; /* no VM in this test */

    ms_threadpool_submit(&tp, job);

    MsJob* done = wait_done(&tp, 2000);
    TEST_ASSERT(done != NULL);
    TEST_ASSERT_EQ(done->error, 0);
    TEST_ASSERT(done->result_buf != NULL);
    TEST_ASSERT_EQ(done->result_len, clen);
    TEST_ASSERT_STR_EQ(done->result_buf, content);

    free(done->result_buf);
    free(done->path);
    free(done);

    ms_threadpool_destroy(&tp);
    remove(path);
    free(path);
}

/* ---- test: write file async ---- */

static void test_write_file(void) {
    const char* content = "written by threadpool";
    size_t clen = strlen(content);

#ifdef _WIN32
    char* path = (char*)malloc(MAX_PATH);
    char tmp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_dir);
    GetTempFileNameA(tmp_dir, "msw", 0, path);
#else
    char* path = (char*)malloc(64);
    snprintf(path, 64, "/tmp/ms_tp_write_%d.bin", (int)getpid());
#endif

    MsThreadPool tp;
    ms_threadpool_init(&tp, 1);

    char* buf = (char*)malloc(clen);
    memcpy(buf, content, clen);

    MsJob* job = (MsJob*)calloc(1, sizeof(MsJob));
    job->kind          = MS_JOB_WRITE_FILE;
    job->path          = tp_strdup(path);
    job->write_buf     = buf;
    job->write_len     = clen;
    job->future_opaque = NULL;

    ms_threadpool_submit(&tp, job);

    MsJob* done = wait_done(&tp, 2000);
    TEST_ASSERT(done != NULL);
    TEST_ASSERT_EQ(done->error, 0);

    free(done->path);
    free(done->write_buf);
    free(done);

    /* verify file contents */
    FILE* f = fopen(path, "rb");
    TEST_ASSERT(f != NULL);
    char verify[64];
    size_t rd = fread(verify, 1, sizeof(verify) - 1, f);
    fclose(f);
    verify[rd] = '\0';
    TEST_ASSERT_EQ(rd, clen);
    TEST_ASSERT_STR_EQ(verify, content);

    ms_threadpool_destroy(&tp);
    remove(path);
    free(path);
}

/* ---- test: missing file returns error ---- */

static void test_read_missing_file(void) {
    MsThreadPool tp;
    ms_threadpool_init(&tp, 1);

    MsJob* job = (MsJob*)calloc(1, sizeof(MsJob));
    job->kind          = MS_JOB_READ_FILE;
    job->path          = tp_strdup("/nonexistent_path/ms_file_does_not_exist.bin");
    job->future_opaque = NULL;

    ms_threadpool_submit(&tp, job);

    MsJob* done = wait_done(&tp, 2000);
    TEST_ASSERT(done != NULL);
    TEST_ASSERT(done->error != 0); /* errno set */
    TEST_ASSERT(done->result_buf == NULL);

    free(done->path);
    free(done);
    ms_threadpool_destroy(&tp);
}

/* ---- test: pop_done returns NULL when empty ---- */

static void test_pop_done_empty(void) {
    MsThreadPool tp;
    ms_threadpool_init(&tp, 1);
    TEST_ASSERT(ms_threadpool_pop_done(&tp) == NULL);
    ms_threadpool_destroy(&tp);
}

/* ---- main ---- */

int main(void) {
    test_init_destroy();
    test_read_file();
    test_write_file();
    test_read_missing_file();
    test_pop_done_empty();
    printf("test_threadpool: all passed\n");
    return 0;
}
