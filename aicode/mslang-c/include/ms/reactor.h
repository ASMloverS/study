#pragma once
/* reactor.h - ASYNC-05 stub: cross-platform IO reactor (epoll/kqueue/IOCP).
 * Full implementation deferred to ASYNC-05-reactor.md.
 *
 * Provides a no-op MsReactor so event_loop.c compiles and links.
 * ms_reactor_poll delegates to ms_platform_sleep_ms (defined in event_loop.c)
 * to avoid pulling platform headers into every translation unit that includes
 * vm.h (which indirectly includes this header).
 */
#include <stdint.h>

typedef struct {
    int _placeholder; /* ASYNC-05 will replace with epoll/kqueue/IOCP handle */
} MsReactor;

/* Implemented in event_loop.c.  Sleeps for at most ms milliseconds. */
void ms_platform_sleep_ms(uint64_t ms);

static inline void ms_reactor_init(MsReactor* r)    { (void)r; }
static inline void ms_reactor_destroy(MsReactor* r) { (void)r; }

static inline void ms_reactor_poll(MsReactor* r, uint64_t timeout_ms) {
    (void)r;
    if (timeout_ms > 0) ms_platform_sleep_ms(timeout_ms);
}
