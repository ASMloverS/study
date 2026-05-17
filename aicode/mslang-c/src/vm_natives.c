#include "ms/vm.h"
#include "ms/event_loop.h"
#include "ms/reactor.h"
#include "ms/value.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/shape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Platform socket headers */
#if defined(_WIN32)
   /* winsock2.h included via vm.h -> event_loop.h -> reactor.h */
#  include <ws2tcpip.h>
   typedef SOCKET ms_sock_t;
#  define MS_INVALID_SOCK  INVALID_SOCKET
#  define MS_SOCK_ERR      SOCKET_ERROR
#  define MS_EAGAIN        WSAEWOULDBLOCK
#  define ms_errno()       WSAGetLastError()
#  define ms_close_sock(s) closesocket(s)
   static int ms_set_nonblocking(ms_sock_t s) {
       u_long mode = 1;
       return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
   }
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
   typedef int ms_sock_t;
#  define MS_INVALID_SOCK  (-1)
#  define MS_SOCK_ERR      (-1)
#  define MS_EAGAIN        EAGAIN
#  define ms_errno()       errno
#  define ms_close_sock(s) close(s)
   static int ms_set_nonblocking(ms_sock_t s) {
       int flags = fcntl(s, F_GETFL, 0);
       if (flags < 0) return -1;
       return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
   }
#endif

static MsValue native_clock(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm); MS_UNUSED(argc); MS_UNUSED(argv);
    return MS_NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static MsValue native_print(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        if (MS_IS_OBJECT(argv[i]))
            ms_object_print(MS_AS_OBJECT(argv[i]));
        else
            ms_value_print(argv[i]);
    }
    printf("\n");
    return MS_NIL_VAL();
}

static MsValue native_type(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(argc);
    if (argc < 1) return MS_OBJ_VAL(ms_obj_string_copy(vm, "nil", 3));
    const char* t = "nil";
    MsValue val = argv[0];
    if (MS_IS_BOOL(val))        { t = "bool"; }
    else if (MS_IS_NUMBER(val)) { t = "number"; }
    else if (MS_IS_INT(val))    { t = "int"; }
    else if (MS_IS_OBJECT(val)) {
        switch (MS_OBJ_TYPE(val)) {
        case MS_OBJ_STRING:   t = "string";   break;
        case MS_OBJ_FUNCTION:
        case MS_OBJ_CLOSURE:
        case MS_OBJ_NATIVE:   t = "function"; break;
        case MS_OBJ_CLASS:    t = "class";    break;
        case MS_OBJ_INSTANCE: t = "instance"; break;
        case MS_OBJ_LIST:     t = "list";     break;
        case MS_OBJ_MAP:      t = "map";      break;
        case MS_OBJ_TUPLE:    t = "tuple";    break;
        default:              t = "object";   break;
        }
    }
    return MS_OBJ_VAL(ms_obj_string_copy(vm, t, (int)strlen(t)));
}

static MsValue native_str(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) return MS_OBJ_VAL(ms_obj_string_copy(vm, "nil", 3));
    if (MS_IS_STRING(argv[0])) return argv[0];
    char* s = ms_value_to_cstring(argv[0]);
    MsObjString* os = ms_obj_string_copy(vm, s, (int)strlen(s));
    free(s);
    return MS_OBJ_VAL(os);
}

static MsValue native_num(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_NUMBER_VAL(0.0);
    if (MS_IS_NUMBER(argv[0])) return argv[0];
    if (MS_IS_INT(argv[0]))    return MS_NUMBER_VAL((double)MS_AS_INT(argv[0]));
    if (MS_IS_STRING(argv[0])) {
        double d = strtod(MS_AS_CSTRING(argv[0]), NULL);
        return MS_NUMBER_VAL(d);
    }
    return MS_NUMBER_VAL(0.0);
}

static MsValue native_int(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_INT_VAL(0);
    if (MS_IS_INT(argv[0]))    return argv[0];
    if (MS_IS_NUMBER(argv[0])) return MS_INT_VAL((ms_i64)MS_AS_NUMBER(argv[0]));
    if (MS_IS_STRING(argv[0])) {
        long long v = strtoll(MS_AS_CSTRING(argv[0]), NULL, 10);
        return MS_INT_VAL((ms_i64)v);
    }
    return MS_INT_VAL(0);
}

static MsValue native_float(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_NUMBER_VAL(0.0);
    if (MS_IS_NUMBER(argv[0])) return argv[0];
    if (MS_IS_INT(argv[0]))    return MS_NUMBER_VAL((double)MS_AS_INT(argv[0]));
    if (MS_IS_STRING(argv[0])) {
        double d = strtod(MS_AS_CSTRING(argv[0]), NULL);
        return MS_NUMBER_VAL(d);
    }
    return MS_NUMBER_VAL(0.0);
}

static MsValue native_len(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) { ms_vm_runtime_error(vm, "len() requires 1 argument."); return MS_NIL_VAL(); }
    MsValue val = argv[0];
    if (MS_IS_STRING(val)) return MS_INT_VAL(MS_AS_STRING(val)->length);
    if (MS_IS_LIST(val))   return MS_INT_VAL(MS_AS_LIST(val)->items.count);
    if (MS_IS_MAP(val))    return MS_INT_VAL(MS_AS_MAP(val)->table.count);
    if (MS_IS_TUPLE(val))  return MS_INT_VAL(MS_AS_TUPLE(val)->count);
    ms_vm_runtime_error(vm, "len() not supported for this type.");
    return MS_NIL_VAL();
}

static MsValue native_input(MsVM* vm, int argc, MsValue* argv) {
    if (argc >= 1 && MS_IS_STRING(argv[0]))
        printf("%s", MS_AS_CSTRING(argv[0]));
    char buf[1024];
    if (!fgets(buf, (int)sizeof(buf), stdin))
        return MS_OBJ_VAL(ms_obj_string_copy(vm, "", 0));
    int len = (int)strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') { buf[len - 1] = '\0'; len--; }
    return MS_OBJ_VAL(ms_obj_string_copy(vm, buf, len));
}

static MsValue native_assert(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) { ms_vm_runtime_error(vm, "assert() requires at least 1 argument."); return MS_NIL_VAL(); }
    MsValue cond = argv[0];
    bool ok = !MS_IS_NIL(cond) && !(MS_IS_BOOL(cond) && !MS_AS_BOOL(cond));
    if (!ok) {
        if (argc >= 2 && MS_IS_STRING(argv[1]))
            ms_vm_runtime_error(vm, "Assertion failed: %s", MS_AS_CSTRING(argv[1]));
        else
            ms_vm_runtime_error(vm, "Assertion failed.");
    }
    return MS_NIL_VAL();
}

static MsValue native_hasattr(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 2 || !MS_IS_INSTANCE(argv[0]) || !MS_IS_STRING(argv[1]))
        return MS_BOOL_VAL(false);
    MsObjInstance* inst = MS_AS_INSTANCE(argv[0]);
    MsObjString*   name = MS_AS_STRING(argv[1]);
    int slot = ms_shape_find_slot(inst->shape, name);
    if (slot >= 0) return MS_BOOL_VAL(true);
    MsValue dummy;
    if (ms_table_get(&inst->klass->methods, name, &dummy)) return MS_BOOL_VAL(true);
    return MS_BOOL_VAL(false);
}

static MsValue native_getattr(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_INSTANCE(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "getattr() requires instance and string name.");
        return MS_NIL_VAL();
    }
    MsObjInstance* inst = MS_AS_INSTANCE(argv[0]);
    MsObjString*   name = MS_AS_STRING(argv[1]);
    int slot = ms_shape_find_slot(inst->shape, name);
    if (slot >= 0)
        return *ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot);
    MsValue method;
    if (ms_table_get(&inst->klass->methods, name, &method)) return method;
    ms_vm_runtime_error(vm, "Attribute '%s' not found.", name->data);
    return MS_NIL_VAL();
}

static MsValue native_setattr(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 3 || !MS_IS_INSTANCE(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "setattr() requires instance, string name, and value.");
        return MS_NIL_VAL();
    }
    MsObjInstance* inst = MS_AS_INSTANCE(argv[0]);
    MsObjString*   name = MS_AS_STRING(argv[1]);
    MsValue        val  = argv[2];
    int slot = ms_shape_find_slot(inst->shape, name);
    if (slot < 0) {
        inst->shape = ms_shape_transition(vm, inst->shape, name);
        slot = ms_shape_find_slot(inst->shape, name);
    }
    if (slot >= MS_SBO_FIELDS && !inst->overflow_fields) {
        int extra = slot - MS_SBO_FIELDS + 1;
        inst->overflow_fields = (MsValue*)calloc((size_t)extra, sizeof(MsValue));
    }
    *ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot) = val;
    return MS_NIL_VAL();
}

/* ---- gather() ---- */

typedef struct MsGatherCtx {
    MsObjFuture* parent;
    MsValue*     results;
    int          total;
    int          remaining;
} MsGatherCtx;

static void gather_on_resolve(MsVM* vm, void* userdata, int index, MsValue result) {
    MsGatherCtx* ctx = (MsGatherCtx*)userdata;
    if (ctx->parent->state != MS_FUTURE_PENDING) return;
    ctx->results[index] = result;
    if (--ctx->remaining == 0) {
        MsObjList* lst = ms_obj_list_from_array(vm, ctx->results, ctx->total);
        ms_future_resolve(vm, ctx->parent, MS_OBJ_VAL((MsObject*)lst));
        free(ctx->results);
        free(ctx);
    }
}

static void gather_on_reject(MsVM* vm, void* userdata, MsValue error) {
    MsGatherCtx* ctx = (MsGatherCtx*)userdata;
    if (ctx->parent->state != MS_FUTURE_PENDING) return;
    ms_future_reject(vm, ctx->parent, error);
    /* results/ctx memory leaks here; a full impl would track and free on GC.
       For now, accept the minor leak since parent is already rejected. */
}

static MsValue native_gather(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "gather() requires a list argument.");
        return MS_NIL_VAL();
    }
    MsObjList* list = MS_AS_LIST(argv[0]);
    int n = list->items.count;
    MsObjFuture* parent = ms_obj_future_new(vm);
    if (n == 0) {
        MsObjList* empty = ms_obj_list_new(vm);
        ms_future_resolve(vm, parent, MS_OBJ_VAL((MsObject*)empty));
        return MS_OBJ_VAL((MsObject*)parent);
    }
    MsGatherCtx* ctx = (MsGatherCtx*)malloc(sizeof(MsGatherCtx));
    if (!ctx) { ms_vm_runtime_error(vm, "gather: out of memory."); return MS_NIL_VAL(); }
    ctx->parent    = parent;
    ctx->results   = (MsValue*)malloc(sizeof(MsValue) * (size_t)n);
    if (!ctx->results) { free(ctx); ms_vm_runtime_error(vm, "gather: out of memory."); return MS_NIL_VAL(); }
    ctx->total     = n;
    ctx->remaining = n;
    for (int i = 0; i < n; i++) ctx->results[i] = MS_NIL_VAL();
    for (int i = 0; i < n; i++) {
        MsValue v = list->items.data[i];
        if (!MS_IS_FUTURE(v)) {
            free(ctx->results); free(ctx);
            ms_vm_runtime_error(vm, "gather: list must contain only Future objects.");
            return MS_NIL_VAL();
        }
        MsObjFuture* f = MS_AS_FUTURE(v);
        if (f->state == MS_FUTURE_RESOLVED) {
            gather_on_resolve(vm, ctx, i, f->result);
        } else if (f->state == MS_FUTURE_REJECTED) {
            gather_on_reject(vm, ctx, f->result);
            break;
        } else {
            ms_future_add_cb_waiter(vm, f, gather_on_resolve, gather_on_reject, ctx, i);
        }
    }
    return MS_OBJ_VAL((MsObject*)parent);
}

static MsValue native_resume(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_COROUTINE(argv[0])) {
        ms_vm_runtime_error(vm, "resume: expected coroutine as first argument.");
        return MS_NIL_VAL();
    }
    MsObjCoroutine* co = MS_AS_COROUTINE(argv[0]);
    MsValue sent = (argc >= 2) ? argv[1] : MS_NIL_VAL();
    MsValue result = MS_NIL_VAL();
    ms_vm_coro_resume(vm, co, sent, &result);
    return result;
}

/* ---- sleep(ms) - returns a Future resolved after delay_ms ---- */

static MsValue native_sleep(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || (!MS_IS_INT(argv[0]) && !MS_IS_NUMBER(argv[0]))) {
        ms_vm_runtime_error(vm, "sleep() requires a numeric argument.");
        return MS_NIL_VAL();
    }
    uint64_t delay = MS_IS_INT(argv[0])
                     ? (uint64_t)MS_AS_INT(argv[0])
                     : (uint64_t)MS_AS_NUMBER(argv[0]);
    if (!vm->loop_inited) {
        ms_loop_init(&vm->event_loop, vm);
        vm->loop_inited = true;
    }
    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_loop_call_later(&vm->event_loop, delay, fut);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- run_until_complete(future) - drives EventLoop until future done ---- */

static MsValue native_run_until_complete(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_FUTURE(argv[0])) {
        ms_vm_runtime_error(vm, "run_until_complete() requires a Future argument.");
        return MS_NIL_VAL();
    }
    if (!vm->loop_inited) {
        ms_loop_init(&vm->event_loop, vm);
        vm->loop_inited = true;
    }
    MsObjFuture* fut = MS_AS_FUTURE(argv[0]);
    int r = ms_loop_run_until_complete(&vm->event_loop, fut);
    if (r != 0 /* MS_INTERPRET_OK */) return MS_NIL_VAL();
    return fut->result;
}

/* ---- TCP socket natives (ASYNC-06) ---- */

static void ensure_loop(MsVM* vm) {
    if (!vm->loop_inited) {
        ms_loop_init(&vm->event_loop, vm);
        vm->loop_inited = true;
    }
}

/* Create a non-blocking TCP socket. Returns MS_INVALID_SOCK on failure. */
static ms_sock_t make_tcp_socket(void) {
#if defined(_WIN32)
    static bool wsa_inited = false;
    if (!wsa_inited) {
        WSADATA wsd;
        WSAStartup(MAKEWORD(2, 2), &wsd);
        wsa_inited = true;
    }
    ms_sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    ms_sock_t s = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (s == MS_INVALID_SOCK) return s;
    ms_set_nonblocking(s);
    return s;
}

/* ---- tcp_listen(port) -> Future<Socket> ---- */

static MsValue native_tcp_listen(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || (!MS_IS_INT(argv[0]) && !MS_IS_NUMBER(argv[0]))) {
        ms_vm_runtime_error(vm, "tcp_listen() requires a port number.");
        return MS_NIL_VAL();
    }
    int port = MS_IS_INT(argv[0]) ? (int)MS_AS_INT(argv[0])
                                  : (int)MS_AS_NUMBER(argv[0]);
    ensure_loop(vm);

    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_sock_t s = make_tcp_socket();
    if (s == MS_INVALID_SOCK) {
        ms_future_reject(vm, fut,
            MS_OBJ_VAL((MsObject*)ms_obj_string_copy(vm, "socket() failed", 15)));
        return MS_OBJ_VAL((MsObject*)fut);
    }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((unsigned short)port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(s, SOMAXCONN) != 0) {
        ms_close_sock(s);
        ms_future_reject(vm, fut,
            MS_OBJ_VAL((MsObject*)ms_obj_string_copy(vm, "bind/listen failed", 18)));
        return MS_OBJ_VAL((MsObject*)fut);
    }

    MsObjSocket* sock = ms_obj_socket_new(vm, (int)s);
    sock->listening = true;
    ms_future_resolve(vm, fut, MS_OBJ_VAL((MsObject*)sock));
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- tcp_connect(host, port) -> Future<Socket> ---- */

static MsValue native_tcp_connect(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) ||
        (!MS_IS_INT(argv[1]) && !MS_IS_NUMBER(argv[1]))) {
        ms_vm_runtime_error(vm, "tcp_connect() requires (host, port).");
        return MS_NIL_VAL();
    }
    const char* host = MS_AS_CSTRING(argv[0]);
    int port = MS_IS_INT(argv[1]) ? (int)MS_AS_INT(argv[1])
                                  : (int)MS_AS_NUMBER(argv[1]);
    ensure_loop(vm);

    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_sock_t s = make_tcp_socket();
    if (s == MS_INVALID_SOCK) {
        ms_future_reject(vm, fut,
            MS_OBJ_VAL((MsObject*)ms_obj_string_copy(vm, "socket() failed", 15)));
        return MS_OBJ_VAL((MsObject*)fut);
    }

    /* Resolve host synchronously (v1 acceptable) */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL) {
        ms_close_sock(s);
        ms_future_reject(vm, fut,
            MS_OBJ_VAL((MsObject*)ms_obj_string_copy(vm, "getaddrinfo failed", 18)));
        return MS_OBJ_VAL((MsObject*)fut);
    }

    /* Create socket object early so callback can close it on error */
    MsObjSocket* sock = ms_obj_socket_new(vm, (int)s);

    int rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);

    int conn_err = ms_errno();
    if (rc == 0) {
        /* Immediate connect (rare but possible on loopback) */
        sock->connected = true;
        ms_future_resolve(vm, fut, MS_OBJ_VAL((MsObject*)sock));
        return MS_OBJ_VAL((MsObject*)fut);
    }

#if defined(_WIN32)
    if (conn_err != WSAEWOULDBLOCK) {
#else
    if (conn_err != EINPROGRESS) {
#endif
        ms_obj_socket_close(vm, sock);
        ms_future_reject(vm, fut,
            MS_OBJ_VAL((MsObject*)ms_obj_string_copy(vm, "connect failed", 14)));
        return MS_OBJ_VAL((MsObject*)fut);
    }

    sock->write_future = fut;
    ms_reactor_register(&vm->event_loop.reactor, (int)s, MS_IO_WRITABLE, sock);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- socket.accept() -> Future<Socket> ---- */

static MsValue native_socket_accept(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_SOCKET(argv[0])) {
        ms_vm_runtime_error(vm, "accept() requires a socket receiver.");
        return MS_NIL_VAL();
    }
    MsObjSocket* srv = MS_AS_SOCKET(argv[0]);
    if (srv->closed) {
        ms_vm_runtime_error(vm, "accept() on closed socket.");
        return MS_NIL_VAL();
    }
    ensure_loop(vm);

    MsObjFuture* fut = ms_obj_future_new(vm);
    srv->read_future = fut;
    ms_reactor_register(&vm->event_loop.reactor, srv->fd, MS_IO_READABLE, srv);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- socket.read(n) -> Future<bytes> ---- */

static MsValue native_socket_read(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_SOCKET(argv[0]) ||
        (!MS_IS_INT(argv[1]) && !MS_IS_NUMBER(argv[1]))) {
        ms_vm_runtime_error(vm, "read() requires (socket, n).");
        return MS_NIL_VAL();
    }
    MsObjSocket* sock = MS_AS_SOCKET(argv[0]);
    if (sock->closed) {
        ms_vm_runtime_error(vm, "read() on closed socket.");
        return MS_NIL_VAL();
    }
    ensure_loop(vm);

    MsObjFuture* fut = ms_obj_future_new(vm);
    sock->read_future = fut;
    ms_reactor_register(&vm->event_loop.reactor, sock->fd, MS_IO_READABLE, sock);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- socket.write(bytes) -> Future<int> ---- */

static MsValue native_socket_write(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_SOCKET(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "write() requires (socket, string).");
        return MS_NIL_VAL();
    }
    MsObjSocket* sock = MS_AS_SOCKET(argv[0]);
    if (sock->closed) {
        ms_vm_runtime_error(vm, "write() on closed socket.");
        return MS_NIL_VAL();
    }
    MsObjString* data = MS_AS_STRING(argv[1]);
    ensure_loop(vm);

    MsObjFuture* fut = ms_obj_future_new(vm);

    int sent = (int)send((ms_sock_t)(uintptr_t)(unsigned)sock->fd,
                         data->data, (size_t)data->length, 0);
    if (sent > 0) {
        ms_future_resolve(vm, fut, MS_INT_VAL((ms_i64)sent));
        return MS_OBJ_VAL((MsObject*)fut);
    }
    int err = ms_errno();
    if (sent == MS_SOCK_ERR && err != MS_EAGAIN) {
        ms_future_reject(vm, fut,
            MS_OBJ_VAL((MsObject*)ms_obj_string_copy(vm, "send failed", 11)));
        return MS_OBJ_VAL((MsObject*)fut);
    }

    /* Buffer full: wait for WRITABLE */
    sock->write_future = fut;
    ms_reactor_register(&vm->event_loop.reactor, sock->fd, MS_IO_WRITABLE, sock);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- socket.close() -> nil (synchronous) ---- */

static MsValue native_socket_close(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_SOCKET(argv[0])) {
        ms_vm_runtime_error(vm, "close() requires a socket receiver.");
        return MS_NIL_VAL();
    }
    ms_obj_socket_close(vm, MS_AS_SOCKET(argv[0]));
    return MS_NIL_VAL();
}

void ms_vm_register_natives(MsVM* vm) {
    ms_vm_define_native(vm, "clock",   native_clock,    0);
    ms_vm_define_native(vm, "print",   native_print,   -1);
    ms_vm_define_native(vm, "type",    native_type,     1);
    ms_vm_define_native(vm, "str",     native_str,      1);
    ms_vm_define_native(vm, "num",     native_num,      1);
    ms_vm_define_native(vm, "int",     native_int,      1);
    ms_vm_define_native(vm, "float",   native_float,    1);
    ms_vm_define_native(vm, "len",     native_len,      1);
    ms_vm_define_native(vm, "input",   native_input,   -1);
    ms_vm_define_native(vm, "assert",  native_assert,  -1);
    ms_vm_define_native(vm, "hasattr", native_hasattr,  2);
    ms_vm_define_native(vm, "getattr", native_getattr,  2);
    ms_vm_define_native(vm, "setattr", native_setattr,  3);
    ms_vm_define_native(vm, "resume",  native_resume,  -1);
    ms_vm_define_native(vm, "gather",  native_gather,   1);
    ms_vm_define_native(vm, "sleep",              native_sleep,              1);
    ms_vm_define_native(vm, "run_until_complete", native_run_until_complete, 1);
    /* TCP socket natives (ASYNC-06) */
    ms_vm_define_native(vm, "tcp_listen",         native_tcp_listen,         1);
    ms_vm_define_native(vm, "tcp_connect",        native_tcp_connect,        2);
    /* Socket method dispatch: called as free functions with socket as argv[0] */
    ms_vm_define_native(vm, "_socket_accept",     native_socket_accept,      1);
    ms_vm_define_native(vm, "_socket_read",       native_socket_read,        2);
    ms_vm_define_native(vm, "_socket_write",      native_socket_write,       2);
    ms_vm_define_native(vm, "_socket_close",      native_socket_close,       1);
    /* legacy aliases */
    ms_vm_define_native(vm, "tostring", native_str,     1);
    ms_vm_define_native(vm, "toint",    native_int,     1);
    ms_vm_define_native(vm, "tofloat",  native_float,   1);
}
