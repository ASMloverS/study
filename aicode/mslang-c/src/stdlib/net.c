#include "ms/module.h"
#include "ms/vm.h"
#include "ms/event_loop.h"
#include "ms/reactor.h"
#include "ms/value.h"
#include "ms/object.h"
#include <string.h>

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

/* ---- net.listen(port) -> Future<Socket> ---- */

static MsValue native_net_listen(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || (!MS_IS_INT(argv[0]) && !MS_IS_NUMBER(argv[0]))) {
        ms_vm_runtime_error(vm, "net.listen() requires a port number.");
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

/* ---- net.connect(host, port) -> Future<Socket> ---- */

static MsValue native_net_connect(MsVM* vm, int argc, MsValue* argv) {
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

static const MsNativeDef net_defs[] = {
    { "listen",  native_net_listen,  1 },
    { "connect", native_net_connect, 2 },
    { NULL, NULL, 0 }
};

void ms_module_net_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, net_defs);
}
