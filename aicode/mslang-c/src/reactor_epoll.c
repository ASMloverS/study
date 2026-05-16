/* reactor_epoll.c - Linux epoll reactor (ASYNC-05). */
#include "ms/reactor.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>

/* MsReactor struct is defined in reactor.h (Linux branch). */

int ms_reactor_init(MsReactor* r) {
    r->epfd = epoll_create1(EPOLL_CLOEXEC);
    return r->epfd >= 0 ? 0 : -1;
}

void ms_reactor_destroy(MsReactor* r) {
    if (r->epfd >= 0) {
        close(r->epfd);
        r->epfd = -1;
    }
}

int ms_reactor_register(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    struct epoll_event ev;
    ev.events  = 0;
    if (events & MS_IO_READABLE) ev.events |= EPOLLIN;
    if (events & MS_IO_WRITABLE) ev.events |= EPOLLOUT;
    ev.data.ptr = user_data;
    return epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev);
}

int ms_reactor_modify(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    struct epoll_event ev;
    ev.events  = 0;
    if (events & MS_IO_READABLE) ev.events |= EPOLLIN;
    if (events & MS_IO_WRITABLE) ev.events |= EPOLLOUT;
    ev.data.ptr = user_data;
    return epoll_ctl(r->epfd, EPOLL_CTL_MOD, fd, &ev);
}

int ms_reactor_unregister(MsReactor* r, int fd) {
    return epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, NULL);
}

int ms_reactor_arm(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    return ms_reactor_register(r, fd, events, user_data);
}

int ms_reactor_submit_read(MsReactor* r, int fd, void* buf, int len, void* user_data) {
    /* On Linux we arm the fd; the caller reads when the event fires. */
    (void)buf; (void)len;
    return ms_reactor_register(r, fd, MS_IO_READABLE, user_data);
}

int ms_reactor_submit_write(MsReactor* r, int fd, const void* buf, int len, void* user_data) {
    (void)buf; (void)len;
    return ms_reactor_register(r, fd, MS_IO_WRITABLE, user_data);
}

int ms_reactor_poll(MsReactor* r, int64_t timeout_ms,
                    MsIOReadyEvent* out_events, int max_events, int* out_count) {
    struct epoll_event evs[64];
    int cap = max_events < 64 ? max_events : 64;
    int n = epoll_wait(r->epfd, evs, cap, (int)timeout_ms);
    if (n < 0) { *out_count = 0; return -1; }
    for (int i = 0; i < n; i++) {
        out_events[i].fd        = -1;  /* user_data carries identity */
        out_events[i].user_data = evs[i].data.ptr;
        out_events[i].events    = 0;
        if (evs[i].events & EPOLLIN)  out_events[i].events |= MS_IO_READABLE;
        if (evs[i].events & EPOLLOUT) out_events[i].events |= MS_IO_WRITABLE;
        if (evs[i].events & EPOLLERR) out_events[i].events |= MS_IO_ERROR;
    }
    *out_count = n;
    return 0;
}
