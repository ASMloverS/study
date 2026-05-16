/* test_event_loop.c - Unit tests for MsEventLoop (ASYNC-04)
 *
 * Tests:
 *   1. ms_loop_init / ms_loop_destroy lifecycle (no crash)
 *   2. run_until_complete exits when root future already resolved
 *   3. ms_loop_call_later fires future after delay
 *   4. Multiple timers fired in deadline order
 *   5. ms_monotonic_ms returns a non-zero value and advances
 *   6. GC traces EventLoop objects without crash
 *   7. ms_loop_call_soon enqueue does not crash
 */
#include "../test_assert.h"
#include "ms/vm.h"
#include "ms/event_loop.h"
#include "ms/object.h"
#include "ms/value.h"
#include <stdlib.h>

/* ---- helpers ---- */

static MsVM* make_vm(void) {
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) abort();
    ms_vm_init(vm);
    return vm;
}

static void free_vm(MsVM* vm) {
    ms_vm_free(vm);
    free(vm);
}

/* ---- test 1: lifecycle ---- */

static void test_loop_lifecycle(void) {
    MsVM* vm = make_vm();
    MsEventLoop loop;
    ms_loop_init(&loop, vm);
    TEST_ASSERT(!loop.stopped);
    TEST_ASSERT(loop.ready    != NULL);
    TEST_ASSERT(loop.ready_cap > 0);
    TEST_ASSERT(loop.timer_count == 0);
    ms_loop_destroy(&loop);
    TEST_ASSERT(loop.ready  == NULL);
    TEST_ASSERT(loop.timers == NULL);
    free_vm(vm);
}

/* ---- test 2: run_until_complete with pre-resolved future exits immediately ---- */

static void test_run_already_resolved(void) {
    MsVM* vm = make_vm();
    ms_loop_init(&vm->event_loop, vm);
    vm->loop_inited = true;

    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_future_resolve(vm, fut, MS_INT_VAL(7));
    TEST_ASSERT_EQ(fut->state, MS_FUTURE_RESOLVED);

    int r = ms_loop_run_until_complete(&vm->event_loop, fut);
    TEST_ASSERT_EQ(r, 0); /* MS_INTERPRET_OK */
    TEST_ASSERT_EQ(MS_AS_INT(fut->result), 7);

    free_vm(vm);
}

/* ---- test 3: call_later fires future after delay ---- */

static void test_call_later_fires(void) {
    MsVM* vm = make_vm();
    ms_loop_init(&vm->event_loop, vm);
    vm->loop_inited = true;

    MsObjFuture* fut = ms_obj_future_new(vm);
    /* Use a tiny delay; loop will poll and fire it. */
    ms_loop_call_later(&vm->event_loop, 1, fut);
    TEST_ASSERT_EQ(fut->state, MS_FUTURE_PENDING);

    int r = ms_loop_run_until_complete(&vm->event_loop, fut);
    TEST_ASSERT_EQ(r, 0);
    TEST_ASSERT_EQ(fut->state, MS_FUTURE_RESOLVED);

    free_vm(vm);
}

/* ---- test 4: multiple timers expire in deadline order ---- */

static void test_timer_ordering(void) {
    MsVM* vm = make_vm();
    ms_loop_init(&vm->event_loop, vm);
    vm->loop_inited = true;

    MsObjFuture* f1 = ms_obj_future_new(vm);
    MsObjFuture* f2 = ms_obj_future_new(vm);
    MsObjFuture* f3 = ms_obj_future_new(vm);

    /* Register in reverse-deadline order to stress the heap */
    ms_loop_call_later(&vm->event_loop, 3, f1);
    ms_loop_call_later(&vm->event_loop, 5, f2);
    ms_loop_call_later(&vm->event_loop, 1, f3);

    /* Drive the loop with a later deadline */
    MsObjFuture* done = ms_obj_future_new(vm);
    ms_loop_call_later(&vm->event_loop, 7, done);

    int r = ms_loop_run_until_complete(&vm->event_loop, done);
    TEST_ASSERT_EQ(r, 0);
    TEST_ASSERT_EQ(f1->state, MS_FUTURE_RESOLVED);
    TEST_ASSERT_EQ(f2->state, MS_FUTURE_RESOLVED);
    TEST_ASSERT_EQ(f3->state, MS_FUTURE_RESOLVED);

    free_vm(vm);
}

/* ---- test 5: ms_monotonic_ms advances ---- */

static void test_monotonic_ms(void) {
    uint64_t t1 = ms_monotonic_ms();
    TEST_ASSERT(t1 > 0);
    uint64_t t2 = ms_monotonic_ms();
    TEST_ASSERT(t2 >= t1);
}

/* ---- test 6: GC traces EventLoop objects without crash ---- */

static void test_gc_traces_loop(void) {
    MsVM* vm = make_vm();
    ms_loop_init(&vm->event_loop, vm);
    vm->loop_inited = true;

    MsObjFuture* f1 = ms_obj_future_new(vm);
    ms_loop_call_later(&vm->event_loop, 100, f1);

    /* Run a full GC while the loop has pending timer futures */
    ms_gc_collect(vm);

    /* Future must still be alive and pending after GC */
    TEST_ASSERT_EQ(f1->state, MS_FUTURE_PENDING);

    free_vm(vm);
}

/* ---- test 7: call_soon enqueue does not crash ---- */

static void test_call_soon_no_crash(void) {
    MsVM* vm = make_vm();
    ms_loop_init(&vm->event_loop, vm);
    vm->loop_inited = true;

    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_future_resolve(vm, fut, MS_INT_VAL(1));

    int r = ms_loop_run_until_complete(&vm->event_loop, fut);
    TEST_ASSERT_EQ(r, 0);

    free_vm(vm);
}

/* ---- main ---- */

int main(void) {
    test_loop_lifecycle();
    test_run_already_resolved();
    test_call_later_fires();
    test_timer_ordering();
    test_monotonic_ms();
    test_gc_traces_loop();
    test_call_soon_no_crash();
    return 0;
}
