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

int ms_reactor_register(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    if (r->watch_count >= MS_REACTOR_MAX_WATCH) return -1;
    HANDLE h = (HANDLE)(intptr_t)fd;
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (!r->watches[i].active) {
            r->watches[i].handle       = (void*)h;
            r->watches[i].watch_events = events;
            r->watches[i].user_data    = user_data;
            r->watches[i].active       = true;
            r->watch_count++;
            return 0;
        }
    }
    return -1;
}

int ms_reactor_modify(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    HANDLE h = (HANDLE)(intptr_t)fd;
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (r->watches[i].active && (HANDLE)r->watches[i].handle == h) {
            r->watches[i].watch_events = events;
            r->watches[i].user_data    = user_data;
            return 0;
        }
    }
    return -1;
}

int ms_reactor_unregister(MsReactor* r, int fd) {
    HANDLE h = (HANDLE)(intptr_t)fd;
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (r->watches[i].active && (HANDLE)r->watches[i].handle == h) {
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

    /* Step 2: readiness watch via WaitForMultipleObjects */
    if (r->watch_count == 0) {
        DWORD ms = (timeout_ms < 0)  ? INFINITE
                 : (timeout_ms == 0) ? 0
                 : (DWORD)(timeout_ms > 0xFFFFFFFELL ? 0xFFFFFFFEUL : (DWORD)timeout_ms);
        if (ms > 0) Sleep(ms);
        return 0;
    }

    HANDLE handles[MS_REACTOR_MAX_WATCH];
    int    idx_map[MS_REACTOR_MAX_WATCH];
    int    n = 0;
    for (int i = 0; i < MS_REACTOR_MAX_WATCH; i++) {
        if (r->watches[i].active) {
            handles[n] = (HANDLE)r->watches[i].handle;
            idx_map[n] = i;
            n++;
        }
    }

    DWORD ms = (timeout_ms < 0)  ? INFINITE
             : (timeout_ms == 0) ? 0
             : (DWORD)(timeout_ms > 0xFFFFFFFELL ? 0xFFFFFFFEUL : (DWORD)timeout_ms);

    DWORD ret = WaitForMultipleObjects((DWORD)n, handles, FALSE, ms);
    if (ret == WAIT_TIMEOUT || ret == WAIT_FAILED) return 0;

    /* Scan all handles for signalled state */
    for (int i = 0; i < n && *out_count < max_events; i++) {
        if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) {
            int wi = idx_map[i];
            out_events[*out_count].fd        = (int)(intptr_t)handles[i];
            out_events[*out_count].events    = r->watches[wi].watch_events & MS_IO_READABLE;
            out_events[*out_count].user_data = r->watches[wi].user_data;
            (*out_count)++;
        }
    }
    return 0;
}
