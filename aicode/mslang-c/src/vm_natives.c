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

/* Platform socket headers (for socket method dispatch: accept/read/write/close) */
#if defined(_WIN32)
   /* winsock2.h included via vm.h -> event_loop.h -> reactor.h */
#  include <ws2tcpip.h>
   typedef SOCKET ms_sock_t;
#  define MS_SOCK_ERR  SOCKET_ERROR
#  define MS_EAGAIN    WSAEWOULDBLOCK
#  define ms_errno()   WSAGetLastError()
#else
#  include <sys/socket.h>
#  include <unistd.h>
#  include <errno.h>
   typedef int ms_sock_t;
#  define MS_SOCK_ERR  (-1)
#  define MS_EAGAIN    EAGAIN
#  define ms_errno()   errno
#endif

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

/* ---- TCP socket method dispatch (socket.accept/read/write/close) ---- */

static void ensure_loop(MsVM* vm) {
    if (!vm->loop_inited) {
        ms_loop_init(&vm->event_loop, vm);
        vm->loop_inited = true;
    }
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

bool ms_socket_invoke(MsVM* vm, MsValue receiver, MsObjString* method,
                      int argc, MsValue* argv, MsValue* out) {
    if (!MS_IS_SOCKET(receiver)) return false;
    MsValue call_argv[2] = { receiver, argc >= 1 ? argv[0] : MS_NIL_VAL() };
    const char* name = method->data;
    if (strcmp(name, "accept") == 0) {
        *out = native_socket_accept(vm, 1, call_argv);
        return true;
    }
    if (strcmp(name, "read") == 0) {
        *out = native_socket_read(vm, argc + 1, call_argv);
        return true;
    }
    if (strcmp(name, "write") == 0) {
        *out = native_socket_write(vm, argc + 1, call_argv);
        return true;
    }
    if (strcmp(name, "close") == 0) {
        *out = native_socket_close(vm, 1, call_argv);
        return true;
    }
    return false;
}

void ms_vm_register_natives(MsVM* vm) {
    /* Language-core globals (kept) */
    ms_vm_define_native(vm, "print",    native_print,    -1);
    ms_vm_define_native(vm, "input",    native_input,    -1);
    ms_vm_define_native(vm, "assert",   native_assert,   -1);
    ms_vm_define_native(vm, "type",     native_type,      1);
    ms_vm_define_native(vm, "str",      native_str,       1);
    ms_vm_define_native(vm, "tostring", native_str,       1);
    ms_vm_define_native(vm, "num",      native_num,       1);
    ms_vm_define_native(vm, "int",      native_int,       1);
    ms_vm_define_native(vm, "float",    native_float,     1);
    ms_vm_define_native(vm, "toint",    native_int,       1);
    ms_vm_define_native(vm, "tofloat",  native_float,     1);
    ms_vm_define_native(vm, "len",      native_len,       1);
    ms_vm_define_native(vm, "hasattr",  native_hasattr,   2);
    ms_vm_define_native(vm, "getattr",  native_getattr,   2);
    ms_vm_define_native(vm, "setattr",  native_setattr,   3);
    /* clock/sleep/run_until_complete/gather/resume migrated to time module.
       tcp_listen/tcp_connect migrated to net module. */
}
