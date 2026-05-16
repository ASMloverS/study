/* test_future.c - Unit tests for MsObjFuture (ASYNC-03)
 *
 * Tests:
 *   1. ms_obj_future_new() creates a pending future
 *   2. ms_future_resolve() - state RESOLVED, result accessible
 *   3. ms_future_reject()  - state REJECTED, error value set
 *   4. resolve is idempotent
 *   5. MS_WAITER_CB waiter: callback invoked on resolve
 *   6. MS_WAITER_CB waiter: callback invoked on reject
 *   7. ms_obj_list_from_array() builds correct list
 *   8. MS_IS_FUTURE / MS_AS_FUTURE macros work
 */
#include "../test_assert.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include <stdlib.h>
#include <string.h>

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

/* ---- test 1: new future is pending ---- */

static void test_future_new_pending(void) {
    MsVM* vm = make_vm();
    MsObjFuture* fut = ms_obj_future_new(vm);
    TEST_ASSERT(fut != NULL);
    TEST_ASSERT_EQ(fut->state, MS_FUTURE_PENDING);
    TEST_ASSERT(fut->coro    == NULL);
    TEST_ASSERT(fut->waiters == NULL);
    free_vm(vm);
}

/* ---- test 2: resolve sets state and result ---- */

static void test_future_resolve(void) {
    MsVM* vm = make_vm();
    MsObjFuture* fut = ms_obj_future_new(vm);
    MsValue val = MS_INT_VAL(42);
    ms_future_resolve(vm, fut, val);
    TEST_ASSERT_EQ(fut->state, MS_FUTURE_RESOLVED);
    TEST_ASSERT(MS_IS_INT(fut->result));
    TEST_ASSERT_EQ(MS_AS_INT(fut->result), 42);
    TEST_ASSERT(fut->waiters == NULL);
    free_vm(vm);
}

/* ---- test 3: reject sets state and error ---- */

static void test_future_reject(void) {
    MsVM* vm = make_vm();
    MsObjFuture* fut = ms_obj_future_new(vm);
    MsValue err = MS_NUMBER_VAL(99.0);
    ms_future_reject(vm, fut, err);
    TEST_ASSERT_EQ(fut->state, MS_FUTURE_REJECTED);
    TEST_ASSERT(MS_IS_NUMBER(fut->result));
    TEST_ASSERT(fut->waiters == NULL);
    free_vm(vm);
}

/* ---- test 4: resolve-after-resolve is a no-op ---- */

static void test_future_resolve_idempotent(void) {
    MsVM* vm = make_vm();
    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_future_resolve(vm, fut, MS_INT_VAL(1));
    ms_future_resolve(vm, fut, MS_INT_VAL(2));   /* must not change result */
    TEST_ASSERT_EQ(fut->state, MS_FUTURE_RESOLVED);
    TEST_ASSERT_EQ(MS_AS_INT(fut->result), 1);
    free_vm(vm);
}

/* ---- test 5: CB waiter invoked on resolve ---- */

static int cb_called = 0;
static int cb_index_received = -1;

static void test_cb_resolve(MsVM* vm, void* userdata, int index, MsValue result) {
    (void)vm; (void)userdata; (void)result;
    cb_called++;
    cb_index_received = index;
}
static void test_cb_reject(MsVM* vm, void* userdata, MsValue error) {
    (void)vm; (void)userdata; (void)error;
}

static void test_future_cb_waiter_resolve(void) {
    MsVM* vm = make_vm();
    MsObjFuture* fut = ms_obj_future_new(vm);
    cb_called = 0;
    cb_index_received = -1;
    ms_future_add_cb_waiter(vm, fut, test_cb_resolve, test_cb_reject, NULL, 7);
    ms_future_resolve(vm, fut, MS_INT_VAL(0));
    TEST_ASSERT_EQ(cb_called, 1);
    TEST_ASSERT_EQ(cb_index_received, 7);
    free_vm(vm);
}

/* ---- test 6: CB waiter invoked on reject ---- */

static int reject_cb_called = 0;
static void reject_cb(MsVM* vm, void* userdata, MsValue error) {
    (void)vm; (void)userdata; (void)error;
    reject_cb_called++;
}

static void test_future_cb_waiter_reject(void) {
    MsVM* vm = make_vm();
    MsObjFuture* fut = ms_obj_future_new(vm);
    reject_cb_called = 0;
    ms_future_add_cb_waiter(vm, fut, test_cb_resolve, reject_cb, NULL, 0);
    ms_future_reject(vm, fut, MS_NIL_VAL());
    TEST_ASSERT_EQ(reject_cb_called, 1);
    free_vm(vm);
}

/* ---- test 7: ms_obj_list_from_array builds correct list ---- */

static void test_list_from_array(void) {
    MsVM* vm = make_vm();
    MsValue items[3] = { MS_INT_VAL(10), MS_INT_VAL(20), MS_INT_VAL(30) };
    MsObjList* lst = ms_obj_list_from_array(vm, items, 3);
    TEST_ASSERT(lst != NULL);
    TEST_ASSERT_EQ(lst->items.count, 3);
    TEST_ASSERT_EQ(MS_AS_INT(lst->items.data[0]), 10);
    TEST_ASSERT_EQ(MS_AS_INT(lst->items.data[1]), 20);
    TEST_ASSERT_EQ(MS_AS_INT(lst->items.data[2]), 30);
    free_vm(vm);
}

/* ---- test 8: MS_IS_FUTURE / MS_AS_FUTURE macros ---- */

static void test_future_macros(void) {
    MsVM* vm = make_vm();
    MsObjFuture* fut = ms_obj_future_new(vm);
    MsValue v = MS_OBJ_VAL((MsObject*)fut);
    TEST_ASSERT(MS_IS_FUTURE(v));
    TEST_ASSERT(MS_AS_FUTURE(v) == fut);
    /* A non-future value must not match */
    MsValue not_fut = MS_INT_VAL(1);
    TEST_ASSERT(!MS_IS_FUTURE(not_fut));
    free_vm(vm);
}

/* ---- main ---- */

int main(void) {
    test_future_new_pending();
    test_future_resolve();
    test_future_reject();
    test_future_resolve_idempotent();
    test_future_cb_waiter_resolve();
    test_future_cb_waiter_reject();
    test_list_from_array();
    test_future_macros();
    return 0;
}
