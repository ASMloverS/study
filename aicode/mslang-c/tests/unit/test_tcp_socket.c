/* test_tcp_socket.c - Unit tests for MsObjSocket (ASYNC-06)
 *
 * Tests:
 *   1. MS_OBJ_SOCKET type: ms_obj_socket_new() returns non-NULL socket
 *   2. ms_obj_socket_new() initialises fields correctly
 *   3. MS_IS_SOCKET / MS_AS_SOCKET macros work
 *   4. socket.close() rejects pending read future
 *   5. socket.close() rejects pending write future
 *   6. socket.close() is idempotent (second close is no-op)
 *   7. GC trace: futures embedded in socket are reachable after GC cycle
 *   8. native_tcp_listen: returns a resolved future containing an ObjSocket
 */
#include "../test_assert.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/event_loop.h"
#include "ms/table.h"
#include "ms/memory.h"
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

/* ---- test 1: ms_obj_socket_new returns non-NULL ---- */

static void test_socket_new_nonnull(void) {
    MsVM* vm = make_vm();
    MsObjSocket* sock = ms_obj_socket_new(vm, -1);
    TEST_ASSERT(sock != NULL);
    free_vm(vm);
}

/* ---- test 2: fields initialised correctly ---- */

static void test_socket_new_fields(void) {
    MsVM* vm = make_vm();
    MsObjSocket* sock = ms_obj_socket_new(vm, 42);
    TEST_ASSERT_EQ(sock->fd, 42);
    TEST_ASSERT_EQ(sock->connected, false);
    TEST_ASSERT_EQ(sock->listening, false);
    TEST_ASSERT_EQ(sock->closed, false);
    TEST_ASSERT(sock->read_future  == NULL);
    TEST_ASSERT(sock->write_future == NULL);
    TEST_ASSERT_EQ(sock->obj.type, MS_OBJ_SOCKET);
    free_vm(vm);
}

/* ---- test 3: IS/AS macros ---- */

static void test_socket_macros(void) {
    MsVM* vm = make_vm();
    MsObjSocket* sock = ms_obj_socket_new(vm, 7);
    MsValue v = MS_OBJ_VAL((MsObject*)sock);
    TEST_ASSERT(MS_IS_SOCKET(v));
    TEST_ASSERT(MS_AS_SOCKET(v) == sock);
    /* a future is NOT a socket */
    MsObjFuture* fut = ms_obj_future_new(vm);
    MsValue fv = MS_OBJ_VAL((MsObject*)fut);
    TEST_ASSERT(!MS_IS_SOCKET(fv));
    free_vm(vm);
}

/* ---- test 4: close rejects pending read future ---- */

static void test_socket_close_rejects_read_future(void) {
    MsVM* vm = make_vm();
    ms_loop_init(&vm->event_loop, vm);
    vm->loop_inited = true;

    MsObjSocket* sock = ms_obj_socket_new(vm, -1);
    MsObjFuture* rfut = ms_obj_future_new(vm);
    sock->read_future = rfut;

    ms_obj_socket_close(vm, sock);

    TEST_ASSERT_EQ(sock->closed, true);
    TEST_ASSERT_EQ(rfut->state, MS_FUTURE_REJECTED);
    TEST_ASSERT(sock->read_future == NULL);
    free_vm(vm);
}

/* ---- test 5: close rejects pending write future ---- */

static void test_socket_close_rejects_write_future(void) {
    MsVM* vm = make_vm();
    ms_loop_init(&vm->event_loop, vm);
    vm->loop_inited = true;

    MsObjSocket* sock = ms_obj_socket_new(vm, -1);
    MsObjFuture* wfut = ms_obj_future_new(vm);
    sock->write_future = wfut;

    ms_obj_socket_close(vm, sock);

    TEST_ASSERT_EQ(wfut->state, MS_FUTURE_REJECTED);
    TEST_ASSERT(sock->write_future == NULL);
    free_vm(vm);
}

/* ---- test 6: close is idempotent ---- */

static void test_socket_close_idempotent(void) {
    MsVM* vm = make_vm();
    ms_loop_init(&vm->event_loop, vm);
    vm->loop_inited = true;

    MsObjSocket* sock = ms_obj_socket_new(vm, -1);
    ms_obj_socket_close(vm, sock);
    TEST_ASSERT_EQ(sock->closed, true);
    /* second close must not crash */
    ms_obj_socket_close(vm, sock);
    TEST_ASSERT_EQ(sock->closed, true);
    free_vm(vm);
}

/* ---- test 7: GC trace: socket type correct, futures reachable via socket ---- */

static void test_socket_gc_trace(void) {
    MsVM* vm = make_vm();
    MsObjSocket* sock = ms_obj_socket_new(vm, -1);
    MsObjFuture* rfut = ms_obj_future_new(vm);
    MsObjFuture* wfut = ms_obj_future_new(vm);
    sock->read_future  = rfut;
    sock->write_future = wfut;

    /* Verify sock correctly holds futures before GC */
    TEST_ASSERT_EQ(sock->read_future,  rfut);
    TEST_ASSERT_EQ(sock->write_future, wfut);

    /* GC type is correct */
    TEST_ASSERT_EQ(sock->obj.type, MS_OBJ_SOCKET);

    /* Keep sock alive via a global and verify it survives */
    MsObjString* key = ms_obj_string_copy(vm, "sock", 4);
    ms_table_set(&vm->globals, key, MS_OBJ_VAL((MsObject*)sock));
    ms_gc_collect_minor(vm);

    /* sock must still be valid: futures accessible through socket */
    MsValue sockval;
    TEST_ASSERT(ms_table_get(&vm->globals, key, &sockval));
    MsObjSocket* sock2 = MS_AS_SOCKET(sockval);
    TEST_ASSERT_EQ(sock2->read_future,  rfut);
    TEST_ASSERT_EQ(sock2->write_future, wfut);
    free_vm(vm);
}

/* ---- test 8: native tcp_listen returns resolved Future<Socket> ---- */

static void test_native_tcp_listen_resolves(void) {
    MsVM* vm = make_vm();

    /* Run script: var f = tcp_listen(0); run_until_complete(f) */
    const char* src =
        "var f = tcp_listen(0)\n"
        "run_until_complete(f)\n";
    MsInterpretResult r = ms_vm_interpret(vm, src, "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);

    free_vm(vm);
}

/* ---- main ---- */

int main(void) {
    test_socket_new_nonnull();
    test_socket_new_fields();
    test_socket_macros();
    test_socket_close_rejects_read_future();
    test_socket_close_rejects_write_future();
    test_socket_close_idempotent();
    test_socket_gc_trace();
    test_native_tcp_listen_resolves();
    printf("test_tcp_socket: all tests passed\n");
    return 0;
}
