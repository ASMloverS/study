#pragma once
/* reactor.h - ASYNC-05: Cross-platform IO reactor (epoll / kqueue / IOCP).
 *
 * Readiness model (epoll/kqueue): register fd, poll fires when readable/writable.
 * Completion model (IOCP): pre-submit buffer, poll fires when IO is done.
 */
#include <stdint.h>
#include <stdbool.h>

/* IO event flags */
typedef enum {
    MS_IO_READABLE = 1 << 0,
    MS_IO_WRITABLE = 1 << 1,
    MS_IO_ERROR    = 1 << 2,
} MsIOEvent;

/* Single ready/completion event returned by ms_reactor_poll */
typedef struct {
    int       fd;         /* triggering fd (POSIX); -1 on IOCP completion */
    MsIOEvent events;
    void*     user_data;  /* pointer registered with the fd */
} MsIOReadyEvent;

/* Concrete MsReactor struct - platform fields selected at compile time.
 * MsEventLoop embeds this by value, so it must be a complete type. */
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winsock2.h>
#  define MS_REACTOR_MAX_WATCH 64
typedef struct {
    bool      active;
    bool      is_socket;  /* true: Winsock SOCKET (use select); false: HANDLE (use WaitForMultipleObjects) */
    void*     handle;     /* HANDLE or SOCKET stored as void* */
    MsIOEvent watch_events;
    void*     user_data;
} MsWatchEntry;

typedef struct MsReactor {
    void*        iocp;    /* HANDLE: CreateIoCompletionPort */
    MsWatchEntry watches[MS_REACTOR_MAX_WATCH];
    int          watch_count;
} MsReactor;

#elif defined(__linux__)
typedef struct MsReactor {
    int epfd;
} MsReactor;

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
typedef struct MsReactor {
    int kqfd;
} MsReactor;

#else
/* Fallback stub: no-op reactor */
typedef struct MsReactor {
    int _placeholder;
} MsReactor;
#endif

/* Lifecycle */
int  ms_reactor_init   (MsReactor* r);
void ms_reactor_destroy(MsReactor* r);

/* Register / modify / unregister fd */
int ms_reactor_register  (MsReactor* r, int fd, MsIOEvent events, void* user_data);
int ms_reactor_modify    (MsReactor* r, int fd, MsIOEvent events, void* user_data);
int ms_reactor_unregister(MsReactor* r, int fd);

/* Readiness-style (epoll / kqueue): arm fd for watch */
int ms_reactor_arm(MsReactor* r, int fd, MsIOEvent events, void* user_data);

/* Completion-style (IOCP; epoll emulates via arm) */
int ms_reactor_submit_read (MsReactor* r, int fd, void* buf, int len, void* user_data);
int ms_reactor_submit_write(MsReactor* r, int fd, const void* buf, int len, void* user_data);

/* Wait for IO events.
 * timeout_ms: 0=immediate, -1=forever, >0=wait up to N ms.
 * Fills out_events[0..max_events); sets *out_count.
 * Returns 0 on success, -1 on system error. */
int ms_reactor_poll(MsReactor* r, int64_t timeout_ms,
                    MsIOReadyEvent* out_events, int max_events, int* out_count);
