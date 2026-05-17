/* reactor_iocp.c - Windows IOCP + WaitForMultipleObjects reactor (ASYNC-05).
 *
 * IOCP handles completion-based IO (WSARecv/WSASend + OVERLAPPED).
 * WaitForMultipleObjects handles readiness-based watch (pipes, anonymous handles).
 * ms_reactor_register uses the readiness table; ms_reactor_submit_read/write
 * uses the IOCP path (deferred to ASYNC-06 for real socket ops).
 */
#include "ms/reactor.h"
#include <windows.h>
#include <string.h>

/* MsReactor struct is defined in reactor.h (WIN32 branch). */

int ms_reactor_init(MsReactor* r) {
    r->iocp = (void*)CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (!r->iocp) return -1;
    memset(r->watches, 0, sizeof(r->watches));
    r->watch_count = 0;
    return 0;
}

void ms_reactor_destroy(MsReactor* r) {
    if (r->iocp) {
        CloseHandle((HANDLE)r->iocp);
        r->iocp = NULL;
    }
}

/* Store fd as void* using zero-extension so (SOCKET)(uintptr_t)handle round-trips. */
static void* fd_to_handle(int fd) { return (void*)(uintptr_t)(unsigned)fd; }
static bool  handle_eq_fd(void* h, int fd) { return h == fd_to_handle(fd); }

/* Probe whether fd looks like a valid Winsock socket (vs a kernel HANDLE). */
static bool fd_is_socket(int fd) {
    SOCKET s = (SOCKET)(uintptr_t)(unsigned)fd;
    int type = 0, len = (int)sizeof(type);
    return getsockopt(s, SOL_SOCKET, SO_TYPE, (char*)&type, &len) == 0;
}

int ms_reactor_register(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    if (r->watch_count >= MS_REACTOR_MAX_WATCH) return -1;
    bool is_sock = fd_is_socket(fd);
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (!r->watches[i].active) {
            r->watches[i].handle       = fd_to_handle(fd);
            r->watches[i].watch_events = events;
            r->watches[i].user_data    = user_data;
            r->watches[i].active       = true;
            r->watches[i].is_socket    = is_sock;
            r->watch_count++;
            return 0;
        }
    }
    return -1;
}

int ms_reactor_modify(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (r->watches[i].active && handle_eq_fd(r->watches[i].handle, fd)) {
            r->watches[i].watch_events = events;
            r->watches[i].user_data    = user_data;
            return 0;
        }
    }
    return -1;
}

int ms_reactor_unregister(MsReactor* r, int fd) {
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (r->watches[i].active && handle_eq_fd(r->watches[i].handle, fd)) {
            r->watches[i].active = false;
            r->watch_count--;
            return 0;
        }
    }
    return -1;
}

int ms_reactor_arm(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    return ms_reactor_register(r, fd, events, user_data);
}


int ms_reactor_submit_read(MsReactor* r, int fd, void* buf, int len, void* user_data) {
    (void)r; (void)fd; (void)buf; (void)len; (void)user_data;
    /* Full IOCP overlapped submit deferred to ASYNC-06 (TCP socket layer). */
    return 0;
}

int ms_reactor_submit_write(MsReactor* r, int fd, const void* buf, int len, void* user_data) {
    (void)r; (void)fd; (void)buf; (void)len; (void)user_data;
    return 0;
}

int ms_reactor_poll(MsReactor* r, int64_t timeout_ms,
                    MsIOReadyEvent* out_events, int max_events, int* out_count) {
    *out_count = 0;

    /* Step 1: drain IOCP completions (non-blocking) */
    OVERLAPPED_ENTRY entries[16];
    ULONG removed = 0;
    if (GetQueuedCompletionStatusEx((HANDLE)r->iocp, entries, 16,
                                    &removed, 0, FALSE)) {
        for (ULONG i = 0; i < removed && *out_count < max_events; i++) {
            out_events[*out_count].fd        = -1;
            out_events[*out_count].events    = MS_IO_READABLE;
            out_events[*out_count].user_data = (void*)(uintptr_t)entries[i].lpCompletionKey;
            (*out_count)++;
        }
    }
    if (*out_count > 0) return 0;

    /* Step 2: poll registered fds for readiness.
     * Sockets use select() (WaitForMultipleObjects does not work with Winsock SOCKETs).
     * Kernel HANDLEs (e.g. pipes) use WaitForMultipleObjects. */
    if (r->watch_count == 0) {
        DWORD ms_dw = (timeout_ms <= 0) ? 0
                    : (DWORD)(timeout_ms > 0xFFFFFFFELL ? 0xFFFFFFFEUL : (DWORD)timeout_ms);
        if (ms_dw > 0) Sleep(ms_dw);
        return 0;
    }

    /* --- socket path: select() --- */
    fd_set rset, wset, eset;
    FD_ZERO(&rset); FD_ZERO(&wset); FD_ZERO(&eset);
    int sock_count = 0;
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (!r->watches[i].active || !r->watches[i].is_socket) continue;
        SOCKET s = (SOCKET)(uintptr_t)r->watches[i].handle;
        if (r->watches[i].watch_events & MS_IO_READABLE) FD_SET(s, &rset);
        if (r->watches[i].watch_events & MS_IO_WRITABLE) FD_SET(s, &wset);
        FD_SET(s, &eset);
        sock_count++;
    }

    if (sock_count > 0) {
        struct timeval tv;
        struct timeval* tvp = NULL;
        if (timeout_ms >= 0) {
            tv.tv_sec  = (long)(timeout_ms / 1000);
            tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
            tvp = &tv;
        }
        int sr = select(0, &rset, &wset, &eset, tvp); /* first arg ignored on Windows */
        if (sr > 0) {
            for (int i = 0; i < MS_REACTOR_MAX_WATCH && *out_count < max_events; i++) {
                if (!r->watches[i].active || !r->watches[i].is_socket) continue;
                SOCKET s = (SOCKET)(uintptr_t)r->watches[i].handle;
                MsIOEvent ev = 0;
                if (FD_ISSET(s, &rset)) ev |= MS_IO_READABLE;
                if (FD_ISSET(s, &wset)) ev |= MS_IO_WRITABLE;
                if (FD_ISSET(s, &eset)) ev |= MS_IO_ERROR;
                if (ev != 0) {
                    out_events[*out_count].fd        = (int)(intptr_t)r->watches[i].handle;
                    out_events[*out_count].events    = ev;
                    out_events[*out_count].user_data = r->watches[i].user_data;
                    (*out_count)++;
                }
            }
        }
        if (*out_count > 0) return 0;
    }

    /* --- kernel HANDLE path: WaitForMultipleObjects (for pipes, etc.) --- */
    HANDLE handles[MS_REACTOR_MAX_WATCH];
    int    idx_map[MS_REACTOR_MAX_WATCH];
    int    n = 0;
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (r->watches[i].active && !r->watches[i].is_socket) {
            handles[n] = (HANDLE)(uintptr_t)r->watches[i].handle;
            idx_map[n] = i;
            n++;
        }
    }

    if (n == 0) return 0;

    DWORD ms_dw = (timeout_ms < 0)  ? INFINITE
                : (timeout_ms == 0) ? 0
                : (DWORD)(timeout_ms > 0xFFFFFFFELL ? 0xFFFFFFFEUL : (DWORD)timeout_ms);

    DWORD ret = WaitForMultipleObjects((DWORD)n, handles, FALSE, ms_dw);
    if (ret == WAIT_TIMEOUT || ret == WAIT_FAILED) return 0;

    for (int i = 0; i < n && *out_count < max_events; i++) {
        if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) {
            int wi = idx_map[i];
            out_events[*out_count].fd        = (int)(intptr_t)r->watches[wi].handle;
            out_events[*out_count].events    = r->watches[wi].watch_events & MS_IO_READABLE;
            out_events[*out_count].user_data = r->watches[wi].user_data;
            (*out_count)++;
        }
    }
    return 0;
}
