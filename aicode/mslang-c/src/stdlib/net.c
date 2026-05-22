#include "ms/module.h"
#include "ms/vm.h"
#include "ms/event_loop.h"
#include "ms/reactor.h"
#include "ms/value.h"
#include "ms/object.h"
#include "ms/stdlib/objbuffer.h"
#include <string.h>
#include <stdio.h>

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
   static int ms_net_set_nonblocking(ms_sock_t s) {
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
   static int ms_net_set_nonblocking(ms_sock_t s) {
       int flags = fcntl(s, F_GETFL, 0);
       if (flags < 0) return -1;
       return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
   }
#endif

static void ensure_loop(MsVM* vm) {
    if (!vm->loop_inited) {
        ms_loop_init(&vm->event_loop, vm);
        vm->loop_inited = true;
    }
}

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
    ms_net_set_nonblocking(s);
    return s;
}

/* ---- net.listen(host, port[, backlog]) -> Socket (synchronous) ---- */

static MsValue ms_net_listen(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) ||
        (!MS_IS_INT(argv[1]) && !MS_IS_NUMBER(argv[1]))) {
        ms_vm_runtime_error(vm, "net.listen() requires (host, port[, backlog]).");
        return MS_NIL_VAL();
    }
    const char* host = MS_AS_CSTRING(argv[0]);
    int port    = MS_IS_INT(argv[1]) ? (int)MS_AS_INT(argv[1])
                                     : (int)MS_AS_NUMBER(argv[1]);
    int backlog = (argc >= 3 && (MS_IS_INT(argv[2]) || MS_IS_NUMBER(argv[2])))
                  ? (MS_IS_INT(argv[2]) ? (int)MS_AS_INT(argv[2])
                                        : (int)MS_AS_NUMBER(argv[2]))
                  : 128;

    ensure_loop(vm);

    ms_sock_t s = make_tcp_socket();
    if (s == MS_INVALID_SOCK) {
        ms_vm_runtime_error(vm, "net.listen: socket() failed.");
        return MS_NIL_VAL();
    }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ms_close_sock(s);
        ms_vm_runtime_error(vm, "net.listen: invalid host address '%s'.", host);
        return MS_NIL_VAL();
    }

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(s, backlog) != 0) {
        ms_close_sock(s);
        ms_vm_runtime_error(vm, "net.listen: bind/listen failed.");
        return MS_NIL_VAL();
    }

    MsObjSocket* sock = ms_obj_socket_new(vm, (int)s);
    sock->listening = true;
    return MS_OBJ_VAL((MsObject*)sock);
}

/* ---- net.connect(host, port) -> Future<Socket> ---- */

static MsValue ms_net_connect(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) ||
        (!MS_IS_INT(argv[1]) && !MS_IS_NUMBER(argv[1]))) {
        ms_vm_runtime_error(vm, "net.connect() requires (host, port).");
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

    MsObjSocket* sock = ms_obj_socket_new(vm, (int)s);

    int rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);

    int conn_err = ms_errno();
    if (rc == 0) {
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

/* ---- net.accept(sock) -> Future<Socket> ---- */

static MsValue ms_net_accept(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_SOCKET(argv[0])) {
        ms_vm_runtime_error(vm, "net.accept() requires a Socket.");
        return MS_NIL_VAL();
    }
    MsObjSocket* srv = MS_AS_SOCKET(argv[0]);
    if (srv->closed) {
        ms_vm_runtime_error(vm, "net.accept() on closed socket.");
        return MS_NIL_VAL();
    }
    ensure_loop(vm);

    MsObjFuture* fut = ms_obj_future_new(vm);
    srv->read_future = fut;
    ms_reactor_register(&vm->event_loop.reactor, srv->fd, MS_IO_READABLE, srv);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- net.read(sock, n) -> Future<Buffer> ---- */

static MsValue ms_net_read(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_SOCKET(argv[0]) ||
        (!MS_IS_INT(argv[1]) && !MS_IS_NUMBER(argv[1]))) {
        ms_vm_runtime_error(vm, "net.read() requires (Socket, n).");
        return MS_NIL_VAL();
    }
    MsObjSocket* sock = MS_AS_SOCKET(argv[0]);
    if (sock->closed) {
        ms_vm_runtime_error(vm, "net.read() on closed socket.");
        return MS_NIL_VAL();
    }
    ensure_loop(vm);

    MsObjFuture* fut = ms_obj_future_new(vm);
    sock->read_future = fut;
    ms_reactor_register(&vm->event_loop.reactor, sock->fd, MS_IO_READABLE, sock);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- net.write(sock, data) -> Future<int> ---- */

static MsValue ms_net_write(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_SOCKET(argv[0])) {
        ms_vm_runtime_error(vm, "net.write() requires (Socket, str|Buffer).");
        return MS_NIL_VAL();
    }
    MsObjSocket* sock = MS_AS_SOCKET(argv[0]);
    if (sock->closed) {
        ms_vm_runtime_error(vm, "net.write() on closed socket.");
        return MS_NIL_VAL();
    }

    const char* data_ptr = NULL;
    int data_len = 0;
    if (MS_IS_STRING(argv[1])) {
        MsObjString* s = MS_AS_STRING(argv[1]);
        data_ptr = s->data;
        data_len = s->length;
    } else if (MS_IS_BUFFER(argv[1])) {
        MsObjBuffer* b = MS_AS_BUFFER(argv[1]);
        data_ptr = (const char*)b->data;
        data_len = (int)b->len;
    } else {
        ms_vm_runtime_error(vm, "net.write(): data must be str or Buffer.");
        return MS_NIL_VAL();
    }

    ensure_loop(vm);
    MsObjFuture* fut = ms_obj_future_new(vm);

#if defined(_WIN32)
    int sent = (int)send((ms_sock_t)(uintptr_t)(unsigned)sock->fd,
                         data_ptr, (int)data_len, 0);
#else
    int sent = (int)send((ms_sock_t)sock->fd, data_ptr, (size_t)data_len, 0);
#endif
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

/* ---- net.close(sock) -> nil ---- */

static MsValue ms_net_close(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_SOCKET(argv[0])) {
        ms_vm_runtime_error(vm, "net.close() requires a Socket.");
        return MS_NIL_VAL();
    }
    ms_obj_socket_close(vm, MS_AS_SOCKET(argv[0]));
    return MS_NIL_VAL();
}

/* ---- net.resolve(host) -> list[str] ---- */

static MsValue ms_net_resolve(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "net.resolve() requires a host string.");
        return MS_NIL_VAL();
    }
    const char* host = MS_AS_CSTRING(argv[0]);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        ms_vm_runtime_error(vm, "net.resolve: lookup failed for '%s'.", host);
        return MS_NIL_VAL();
    }

    MsObjList* list = ms_obj_list_new(vm);
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        char ip[INET6_ADDRSTRLEN];
        ip[0] = '\0';
        if (p->ai_family == AF_INET) {
            struct sockaddr_in* sa = (struct sockaddr_in*)p->ai_addr;
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
        } else if (p->ai_family == AF_INET6) {
            struct sockaddr_in6* sa6 = (struct sockaddr_in6*)p->ai_addr;
            inet_ntop(AF_INET6, &sa6->sin6_addr, ip, sizeof(ip));
        }
        if (ip[0] != '\0') {
            MsObjString* s = ms_obj_string_copy(vm, ip, (int)strlen(ip));
            ms_value_array_push(&list->items, MS_OBJ_VAL((MsObject*)s));
        }
    }
    freeaddrinfo(res);
    return MS_OBJ_VAL((MsObject*)list);
}

/* ---- net.tcp_pair() -> list[Socket, Socket] ---- */

static MsValue ms_net_tcp_pair(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    ensure_loop(vm);

    /* Create a loopback server, connect, accept, destroy the server socket. */
#if defined(_WIN32)
    static bool wsa_ok = false;
    if (!wsa_ok) { WSADATA wsd; WSAStartup(MAKEWORD(2,2), &wsd); wsa_ok = true; }
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    int srv = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (srv == MS_INVALID_SOCK) {
        ms_vm_runtime_error(vm, "net.tcp_pair: socket() failed.");
        return MS_NIL_VAL();
    }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0; /* OS picks an ephemeral port */

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(srv, 1) != 0) {
        ms_close_sock(srv);
        ms_vm_runtime_error(vm, "net.tcp_pair: bind/listen failed.");
        return MS_NIL_VAL();
    }

    /* Get the actual bound port */
    int addrlen = (int)sizeof(addr);
    getsockname(srv, (struct sockaddr*)&addr, &addrlen);

    /* Client side (blocking connect for simplicity) */
#if defined(_WIN32)
    SOCKET cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    int cli = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (cli == MS_INVALID_SOCK) {
        ms_close_sock(srv);
        ms_vm_runtime_error(vm, "net.tcp_pair: client socket() failed.");
        return MS_NIL_VAL();
    }

    /* Blocking connect to the loopback server */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(cli, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        ms_close_sock(cli);
        ms_close_sock(srv);
        ms_vm_runtime_error(vm, "net.tcp_pair: connect failed.");
        return MS_NIL_VAL();
    }

    /* Accept the server side */
    struct sockaddr_in peer;
    int peerlen = (int)sizeof(peer);
    ms_sock_t srv_side = (ms_sock_t)accept(srv, (struct sockaddr*)&peer, &peerlen);
    ms_close_sock(srv); /* Server listener no longer needed */
    if (srv_side == MS_INVALID_SOCK) {
        ms_close_sock(cli);
        ms_vm_runtime_error(vm, "net.tcp_pair: accept failed.");
        return MS_NIL_VAL();
    }

    ms_net_set_nonblocking(cli);
    ms_net_set_nonblocking(srv_side);

    MsObjSocket* a = ms_obj_socket_new(vm, (int)cli);
    a->connected = true;
    MsObjSocket* b = ms_obj_socket_new(vm, (int)srv_side);
    b->connected = true;

    MsObjList* pair = ms_obj_list_new(vm);
    ms_value_array_push(&pair->items, MS_OBJ_VAL((MsObject*)a));
    ms_value_array_push(&pair->items, MS_OBJ_VAL((MsObject*)b));
    return MS_OBJ_VAL((MsObject*)pair);
}

static const MsNativeDef net_defs[] = {
    { "listen",   ms_net_listen,   2 },  /* host, port[, backlog] */
    { "connect",  ms_net_connect,  2 },
    { "accept",   ms_net_accept,   1 },
    { "read",     ms_net_read,     2 },
    { "write",    ms_net_write,    2 },
    { "close",    ms_net_close,    1 },
    { "resolve",  ms_net_resolve,  1 },
    { "tcp_pair", ms_net_tcp_pair, 0 },
    { NULL, NULL, 0 }
};

void ms_module_net_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, net_defs);
}
