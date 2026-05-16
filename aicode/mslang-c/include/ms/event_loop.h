#pragma once
/* event_loop.h - ASYNC-04: Single-threaded EventLoop scheduler.
 *
 * Responsibilities:
 *   1. Ready queue   - FIFO ring of coroutines immediately runnable.
 *   2. Timer min-heap - deadline-based future resolution (sleep/timeouts).
 *   3. Reactor poll  - IO readiness (stub until ASYNC-05).
 *   4. run_until_complete - drives the entire async lifecycle.
 *
 * Include order: object.h -> event_loop.h -> vm.h
 * vm.h includes this file (MsVM embeds MsEventLoop), so we must NOT
 * include vm.h here to avoid a circular dependency.
 * ms_loop_run_until_complete returns int:
 *   0 = MS_INTERPRET_OK, 2 = MS_INTERPRET_RUNTIME_ERROR (matches vm.h enum).
 */
#include "ms/object.h"
#include "ms/reactor.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- Timer min-heap entry ---- */

typedef struct {
    uint64_t      deadline_ms;  /* absolute monotonic time in ms */
    MsObjFuture*  future;       /* resolved when deadline passes  */
} MsTimerEntry;

/* ---- EventLoop ---- */

typedef struct MsEventLoop {
    /* Ready queue: circular buffer of coroutine pointers (FIFO) */
    MsObjCoroutine** ready;
    int              ready_head;
    int              ready_tail;
    int              ready_cap;

    /* Timer min-heap (sorted by deadline_ms ascending) */
    MsTimerEntry*    timers;
    int              timer_count;
    int              timer_cap;

    /* Cross-platform reactor (IO poll; ASYNC-05 stub for now) */
    MsReactor        reactor;

    /* True once ms_loop_stop() has been called */
    bool             stopped;

    /* Back-pointer to owning VM (for future-resolve callbacks) */
    struct MsVM*     vm;
} MsEventLoop;

/* ---- Platform time ---- */

/* Monotonic clock in milliseconds. */
uint64_t ms_monotonic_ms(void);

/* ---- Lifecycle ---- */

void ms_loop_init   (MsEventLoop* loop, struct MsVM* vm);
void ms_loop_destroy(MsEventLoop* loop);
void ms_loop_stop   (MsEventLoop* loop);

/* ---- Scheduling ---- */

/* Push coroutine onto the ready queue (O(1) amortised). */
void ms_loop_call_soon(MsEventLoop* loop, MsObjCoroutine* coro);

/* Resolve fut after delay_ms milliseconds via the timer heap. */
void ms_loop_call_later(MsEventLoop* loop, uint64_t delay_ms,
                         MsObjFuture* fut);

/* ---- Main loop ---- */

/* Run until root future is RESOLVED or REJECTED.
   Returns 0 (MS_INTERPRET_OK) on success,
           2 (MS_INTERPRET_RUNTIME_ERROR) on failure.
   Callers that include vm.h can cast to MsInterpretResult directly. */
int ms_loop_run_until_complete(MsEventLoop* loop, MsObjFuture* root);
