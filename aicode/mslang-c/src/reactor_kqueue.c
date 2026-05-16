/* reactor_kqueue.c - macOS/BSD kqueue reactor (ASYNC-05). */
#include "ms/reactor.h"
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

/* MsReactor struct is defined in reactor.h (Apple/BSD branch). */

int ms_reactor_init(MsReactor* r) {
    r->kqfd = kqueue();
    return r->kqfd >= 0 ? 0 : -1;
}

void ms_reactor_destroy(MsReactor* r) {
    if (r->kqfd >= 0) {
        close(r->kqfd);
        r->kqfd = -1;
    }
}

int ms_reactor_register(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    struct kevent kev[2];
    int n = 0;
    if (events & MS_IO_READABLE)
        EV_SET(&kev[n++], (uintptr_t)fd, EVFILT_READ,  EV_ADD | EV_ENABLE, 0, 0, user_data);
    if (events & MS_IO_WRITABLE)
        EV_SET(&kev[n++], (uintptr_t)fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, user_data);
    return n > 0 ? kevent(r->kqfd, kev, n, NULL, 0, NULL) : 0;
}

int ms_reactor_modify(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    return ms_reactor_register(r, fd, events, user_data);
}

int ms_reactor_unregister(MsReactor* r, int fd) {
    struct kevent kev[2];
    EV_SET(&kev[0], (uintptr_t)fd, EVFILT_READ,  EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], (uintptr_t)fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    /* Ignore ENOENT - filter may not be registered */
    kevent(r->kqfd, kev, 2, NULL, 0, NULL);
    return 0;
}

int ms_reactor_arm(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    return ms_reactor_register(r, fd, events, user_data);
}

int ms_reactor_submit_read(MsReactor* r, int fd, void* buf, int len, void* user_data) {
    (void)buf; (void)len;
    return ms_reactor_register(r, fd, MS_IO_READABLE, user_data);
}

int ms_reactor_submit_write(MsReactor* r, int fd, const void* buf, int len, void* user_data) {
    (void)buf; (void)len;
    return ms_reactor_register(r, fd, MS_IO_WRITABLE, user_data);
}

int ms_reactor_poll(MsReactor* r, int64_t timeout_ms,
                    MsIOReadyEvent* out_events, int max_events, int* out_count) {
    struct kevent evs[64];
    int cap = max_events < 64 ? max_events : 64;
    struct timespec ts;
    struct timespec* tsp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec  = (time_t)(timeout_ms / 1000);
        ts.tv_nsec = (long)((timeout_ms % 1000) * 1000000L);
        tsp = &ts;
    }
    int n = kevent(r->kqfd, NULL, 0, evs, cap, tsp);
    if (n < 0) { *out_count = 0; return -1; }
    for (int i = 0; i < n; i++) {
        out_events[i].fd        = (int)evs[i].ident;
        out_events[i].user_data = evs[i].udata;
        out_events[i].events    = 0;
        if (evs[i].filter == EVFILT_READ)  out_events[i].events |= MS_IO_READABLE;
        if (evs[i].filter == EVFILT_WRITE) out_events[i].events |= MS_IO_WRITABLE;
        if (evs[i].flags  & EV_ERROR)      out_events[i].events |= MS_IO_ERROR;
    }
    *out_count = n;
    return 0;
}
