/* test_reactor.c - Unit tests for MsReactor (ASYNC-05)
 *
 * Tests:
 *   1. lifecycle: init/destroy does not crash
 *   2. self-trigger: pipe write makes read-fd fire READABLE in poll
 *   3. timeout: poll on idle reactor returns count=0 after timeout_ms
 *   4. unregister: after unregister, fd no longer fires
 */
#include "../test_assert.h"
#include "ms/reactor.h"
#include <stdint.h>

#if defined(_WIN32)
#  include <windows.h>
/* On Windows we use CreatePipe / WriteFile / CloseHandle */
typedef HANDLE pipe_fd_t;
static int pipe_create(pipe_fd_t out[2]) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    return CreatePipe(&out[0], &out[1], &sa, 0) ? 0 : -1;
}
static void pipe_write_byte(pipe_fd_t w) {
    DWORD written;
    WriteFile(w, "x", 1, &written, NULL);
}
static void pipe_close(pipe_fd_t fd) { CloseHandle(fd); }
/* IOCP reactor uses HANDLE, not int fd - cast for the API */
#  define PIPE_READ_FD(h)  ((int)(intptr_t)(h))
#else
#  include <unistd.h>
typedef int pipe_fd_t;
static int pipe_create(pipe_fd_t out[2]) { return pipe(out); }
static void pipe_write_byte(pipe_fd_t w) {
    char b = 'x';
    (void)write(w, &b, 1);
}
static void pipe_close(pipe_fd_t fd) { close(fd); }
#  define PIPE_READ_FD(fd) (fd)
#endif

/* ---- test 1: lifecycle ---- */
static void test_reactor_lifecycle(void) {
    MsReactor r;
    int rc = ms_reactor_init(&r);
    TEST_ASSERT_EQ(rc, 0);
    ms_reactor_destroy(&r);
}

/* ---- test 2: self-trigger via pipe ---- */
static void test_reactor_self_trigger(void) {
    MsReactor r;
    TEST_ASSERT_EQ(ms_reactor_init(&r), 0);

    pipe_fd_t fds[2];
    TEST_ASSERT_EQ(pipe_create(fds), 0);
    /* fds[0] = read end, fds[1] = write end */

    int token = 42;
    int rc = ms_reactor_register(&r, PIPE_READ_FD(fds[0]),
                                 MS_IO_READABLE, &token);
    TEST_ASSERT_EQ(rc, 0);

    /* Trigger readability */
    pipe_write_byte(fds[1]);

    MsIOReadyEvent events[8];
    int count = 0;
    rc = ms_reactor_poll(&r, 200, events, 8, &count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(count >= 1);
    TEST_ASSERT(events[0].events & MS_IO_READABLE);
    TEST_ASSERT(events[0].user_data == &token);

    ms_reactor_unregister(&r, PIPE_READ_FD(fds[0]));
    pipe_close(fds[0]);
    pipe_close(fds[1]);
    ms_reactor_destroy(&r);
}

/* ---- test 3: timeout returns count=0 when no events ---- */
static void test_reactor_timeout(void) {
    MsReactor r;
    TEST_ASSERT_EQ(ms_reactor_init(&r), 0);

    MsIOReadyEvent events[8];
    int count = -1;
    int rc = ms_reactor_poll(&r, 50, events, 8, &count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(count, 0);

    ms_reactor_destroy(&r);
}

/* ---- test 4: unregister silences further events ---- */
static void test_reactor_unregister(void) {
    MsReactor r;
    TEST_ASSERT_EQ(ms_reactor_init(&r), 0);

    pipe_fd_t fds[2];
    TEST_ASSERT_EQ(pipe_create(fds), 0);

    int token = 99;
    TEST_ASSERT_EQ(ms_reactor_register(&r, PIPE_READ_FD(fds[0]),
                                       MS_IO_READABLE, &token), 0);
    ms_reactor_unregister(&r, PIPE_READ_FD(fds[0]));

    /* Write byte - but fd is no longer registered; poll should return 0 */
    pipe_write_byte(fds[1]);

    MsIOReadyEvent events[8];
    int count = -1;
    TEST_ASSERT_EQ(ms_reactor_poll(&r, 50, events, 8, &count), 0);
    TEST_ASSERT_EQ(count, 0);

    pipe_close(fds[0]);
    pipe_close(fds[1]);
    ms_reactor_destroy(&r);
}

/* ---- main ---- */
int main(void) {
    test_reactor_lifecycle();
    test_reactor_self_trigger();
    test_reactor_timeout();
    test_reactor_unregister();
    return 0;
}
