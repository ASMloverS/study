#include "ms/event_loop.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include <stdlib.h>
#include <string.h>

/* ---- Platform time + sleep ---- */

#if defined(_WIN32)
#  include <windows.h>
uint64_t ms_monotonic_ms(void) {
    return (uint64_t)GetTickCount64();
}
void ms_platform_sleep_ms(uint64_t ms) {
    Sleep((DWORD)ms);
}
#elif defined(__APPLE__)
#  include <mach/mach_time.h>
#  include <time.h>
uint64_t ms_monotonic_ms(void) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    uint64_t ns = mach_absolute_time() * tb.numer / tb.denom;
    return ns / 1000000u;
}
void ms_platform_sleep_ms(uint64_t ms) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000L);
    nanosleep(&ts, NULL);
}
#else
#  include <time.h>
uint64_t ms_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}
void ms_platform_sleep_ms(uint64_t ms) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000L);
    nanosleep(&ts, NULL);
}
#endif

/* ---- Ready queue (circular buffer) ---- */

#define LOOP_READY_INIT_CAP 8

static bool ready_is_empty(MsEventLoop* L) {
    return L->ready_head == L->ready_tail;
}

static void ready_grow(MsEventLoop* L) {
    int new_cap = L->ready_cap < LOOP_READY_INIT_CAP
                  ? LOOP_READY_INIT_CAP : L->ready_cap * 2;
    MsObjCoroutine** buf =
        (MsObjCoroutine**)malloc(sizeof(MsObjCoroutine*) * (size_t)new_cap);
    if (!buf) abort();
    /* Copy linearised items into contiguous buffer */
    int count = 0;
    int i = L->ready_head;
    while (i != L->ready_tail) {
        buf[count++] = L->ready[i];
        i = (i + 1) % L->ready_cap;
    }
    free(L->ready);
    L->ready      = buf;
    L->ready_head = 0;
    L->ready_tail = count;
    L->ready_cap  = new_cap;
}

static void ready_enqueue(MsEventLoop* L, MsObjCoroutine* coro) {
    int next = (L->ready_tail + 1) % L->ready_cap;
    if (next == L->ready_head) {
        ready_grow(L);
        /* After grow, recalculate next */
        next = (L->ready_tail + 1) % L->ready_cap;
    }
    L->ready[L->ready_tail] = coro;
    L->ready_tail = next;
}

static MsObjCoroutine* ready_dequeue(MsEventLoop* L) {
    MsObjCoroutine* c = L->ready[L->ready_head];
    L->ready_head = (L->ready_head + 1) % L->ready_cap;
    return c;
}

/* ---- Timer min-heap ---- */

#define LOOP_TIMER_INIT_CAP 8

static void timer_swap(MsTimerEntry* a, MsTimerEntry* b) {
    MsTimerEntry tmp = *a; *a = *b; *b = tmp;
}

static void sift_up(MsTimerEntry* h, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (h[parent].deadline_ms > h[idx].deadline_ms) {
            timer_swap(&h[parent], &h[idx]);
            idx = parent;
        } else break;
    }
}

static void sift_down(MsTimerEntry* h, int idx, int count) {
    while (true) {
        int smallest = idx;
        int l = 2 * idx + 1, r = 2 * idx + 2;
        if (l < count && h[l].deadline_ms < h[smallest].deadline_ms) smallest = l;
        if (r < count && h[r].deadline_ms < h[smallest].deadline_ms) smallest = r;
        if (smallest == idx) break;
        timer_swap(&h[idx], &h[smallest]);
        idx = smallest;
    }
}

static void timer_heap_push(MsEventLoop* L, MsTimerEntry e) {
    if (L->timer_count >= L->timer_cap) {
        int new_cap = L->timer_cap < LOOP_TIMER_INIT_CAP
                      ? LOOP_TIMER_INIT_CAP : L->timer_cap * 2;
        L->timers = (MsTimerEntry*)realloc(L->timers,
                     sizeof(MsTimerEntry) * (size_t)new_cap);
        if (!L->timers) abort();
        L->timer_cap = new_cap;
    }
    L->timers[L->timer_count++] = e;
    sift_up(L->timers, L->timer_count - 1);
}

static MsTimerEntry timer_heap_pop(MsEventLoop* L) {
    MsTimerEntry top   = L->timers[0];
    L->timers[0]       = L->timers[--L->timer_count];
    if (L->timer_count > 0) sift_down(L->timers, 0, L->timer_count);
    return top;
}

/* Compute wait duration until next timer (or 1000ms if no timers). */
static uint64_t next_timer_timeout(MsEventLoop* L, uint64_t now) {
    if (L->timer_count == 0) return 1000;
    uint64_t deadline = L->timers[0].deadline_ms;
    return deadline > now ? deadline - now : 0;
}

/* ---- Lifecycle ---- */

void ms_loop_init(MsEventLoop* loop, struct MsVM* vm) {
    loop->ready      = (MsObjCoroutine**)malloc(
                        sizeof(MsObjCoroutine*) * LOOP_READY_INIT_CAP);
    if (!loop->ready) abort();
    loop->ready_head = 0;
    loop->ready_tail = 0;
    loop->ready_cap  = LOOP_READY_INIT_CAP;
    loop->timers     = NULL;
    loop->timer_count = 0;
    loop->timer_cap   = 0;
    ms_reactor_init(&loop->reactor);
    loop->stopped    = false;
    loop->vm         = vm;
}

void ms_loop_destroy(MsEventLoop* loop) {
    free(loop->ready);
    loop->ready     = NULL;
    loop->ready_cap = 0;
    free(loop->timers);
    loop->timers     = NULL;
    loop->timer_count = 0;
    loop->timer_cap   = 0;
    ms_reactor_destroy(&loop->reactor);
}

void ms_loop_stop(MsEventLoop* loop) {
    loop->stopped = true;
}

/* ---- Scheduling ---- */

void ms_loop_call_soon(MsEventLoop* loop, MsObjCoroutine* coro) {
    if (!loop->ready) return; /* loop not initialised; ignore */
    ready_enqueue(loop, coro);
}

void ms_loop_call_later(MsEventLoop* loop, uint64_t delay_ms,
                         MsObjFuture* fut) {
    MsTimerEntry e;
    e.deadline_ms = ms_monotonic_ms() + delay_ms;
    e.future      = fut;
    timer_heap_push(loop, e);
}

/* ---- Main loop ---- */

int ms_loop_run_until_complete(MsEventLoop* loop, MsObjFuture* root) {
    struct MsVM* vm = loop->vm;

    while (!loop->stopped) {
        /* 1. Drain ready queue */
        while (!ready_is_empty(loop)) {
            MsObjCoroutine* coro = ready_dequeue(loop);
            MsValue out = MS_NIL_VAL();
            ms_vm_coro_resume(vm, coro, MS_NIL_VAL(), &out);
            /* MS_INTERPRET_RUNTIME_ERROR: error printed by vm; coro is dead.
               MS_INTERPRET_AWAIT: coro registered as waiter; re-queued on
               resolve (see ms_future_resolve in object.c).
               MS_INTERPRET_OK: coro returned normally. */
        }

        /* 2. Fire expired timers */
        uint64_t now = ms_monotonic_ms();
        while (loop->timer_count > 0 &&
               loop->timers[0].deadline_ms <= now) {
            MsTimerEntry entry = timer_heap_pop(loop);
            ms_future_resolve(vm, entry.future, MS_NIL_VAL());
            /* resolve() pushes waiters onto ready queue via ms_loop_call_soon */
        }

        /* 3. Exit if root future is done */
        if (root->state != MS_FUTURE_PENDING) break;

        /* 4. Poll IO if ready queue is still empty */
        if (ready_is_empty(loop)) {
            int64_t timeout = (int64_t)next_timer_timeout(loop, ms_monotonic_ms());
            MsIOReadyEvent io_evs[64];
            int io_count = 0;
            ms_reactor_poll(&loop->reactor, timeout, io_evs, 64, &io_count);
            /* io_evs processing (future resolve) is handled by ASYNC-06 socket layer;
               here we only need the sleep/wakeup behaviour. */
        }
    }

    return root->state == MS_FUTURE_RESOLVED ? 0 : 2;
}
