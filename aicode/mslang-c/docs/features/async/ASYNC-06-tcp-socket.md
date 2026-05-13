# ASYNC-06: TCP Socket 绑定

## ObjSocket（`include/ms/object.h`, `src/object.c`）

### 定义

`MsIOCallback` 内嵌到 socket，跟随 socket 的 GC 生命周期，无需独立分配/手动回收：

```c
typedef struct {
    MsObjFuture* read_future;    // 当前挂起的 read future（无则 NULL）
    MsObjFuture* write_future;   // 当前挂起的 write future（无则 NULL）
} MsIOCallback;

typedef struct {
    MsObject    obj;        // GC header，type = MS_OBJ_SOCKET
    int         fd;         // POSIX fd；Windows 用 SOCKET（uintptr_t）转存
    bool        connected;
    bool        listening;
    bool        closed;
    MsIOCallback read_cb;   // 内嵌读回调（跟随 socket 释放）
    MsIOCallback write_cb;  // 内嵌写回调（跟随 socket 释放）
} MsObjSocket;
```

### 生命周期

```c
MsObjSocket* ms_obj_socket_new(MsVM* vm, int fd);
// print: <Socket fd=N connected/listening/closed>
// free:  若未关闭则 close(fd)/closesocket(fd)；read_cb/write_cb 随之失效
// gc mark: 需 mark read_cb.read_future / write_cb.write_future（见下方 GC 节）
```

### GC trace（`src/vm_gc.c`）

```c
case MS_OBJ_SOCKET: {
    MsObjSocket* s = (MsObjSocket*)obj;
    if (s->read_cb.read_future)   ms_mark_object(vm, (MsObject*)s->read_cb.read_future);
    if (s->write_cb.write_future) ms_mark_object(vm, (MsObject*)s->write_cb.write_future);
    break;
}
```

取消 `vm->io_callbacks` 动态数组方案；socket 本身已是 GC 对象，trace 即可覆盖其内嵌回调。

## 原生函数（`src/vm_natives.c`）

所有 TCP 原生函数均返回 `ObjFuture`，配合 `await` 使用。

### `tcp_connect(host, port) -> Future<Socket>`

```c
static MsValue native_tcp_connect(MsVM* vm, int argc, MsValue* argv) {
    const char* host = MS_AS_CSTRING(argv[0]);
    int port = (int)MS_AS_INT(argv[1]);

    MsObjFuture* fut = ms_obj_future_new(vm);
    // 非阻塞 connect
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    // getaddrinfo（同步，v1 可接受；v2 用线程池）
    // connect(fd, ...) → EINPROGRESS
    // 向 reactor 注册 fd WRITABLE 事件，user_data = { fut, fd, CONNECT }
    // 当 writable 触发：getsockopt(SO_ERROR) 检查 → resolve(fut, socket_obj) 或 reject
    ms_reactor_register(&vm->event_loop->reactor, fd, MS_IO_WRITABLE, callback);
    return MS_OBJECT_VAL((MsObject*)fut);
}
```

### `tcp_listen(port) -> Future<Socket>`

```c
// 创建 server socket，bind, listen（均同步操作，快速完成）
// 在返回前立即 resolve future — 无需等待任何 IO 事件。
// 以 future 形式返回是为与其他 tcp_* API 保持 await 调用一致性，
// 允许用户统一写 var srv = await tcp_listen(9999)。
```

### `socket.accept() -> Future<Socket>`

```c
// 向 reactor 注册 server_fd READABLE 事件
// 就绪时 accept(server_fd) → 新 client fd（非阻塞）
// resolve future，值为新 ObjSocket
```

### `socket.read(n) -> Future<bytes>`

```c
// 向 reactor 注册 fd READABLE 事件（POSIX）/ 投递 submit_read（IOCP）
// 就绪时 recv(fd, buf, n, 0)
// resolve future，值为 ObjString（raw bytes）
// 若 recv == 0：EOF，resolve 为空字符串 ""（非 nil，避免与 nil 语义冲突）
//   调用方以 data.len() == 0 判断连接关闭
// 若 recv < 0（EAGAIN 除外）：reject future，值为错误字符串
```

### `socket.write(bytes) -> Future<int>`

```c
// 尝试 send(fd, data, len, 0)
// 若 EAGAIN/EWOULDBLOCK：注册 WRITABLE 事件，就绪时重试
// resolve future，值为实际写入字节数
```

### `socket.close() -> nil`（同步）

```c
if (!sock->closed) {
    // 必须先 unregister 再 close：close 后 fd 可能被复用，旧 callback 触发会写脏 future
    ms_reactor_unregister(&vm->event_loop.reactor, sock->fd);
    close(sock->fd);    // Windows: closesocket(sock->fd)
    sock->fd     = -1;
    sock->closed = true;
    // 悬空的挂起 future 视为 reject（连接已断）
    if (sock->read_cb.read_future)
        ms_future_reject(vm, sock->read_cb.read_future, MS_STRING_VAL("socket closed"));
    if (sock->write_cb.write_future)
        ms_future_reject(vm, sock->write_cb.write_future, MS_STRING_VAL("socket closed"));
    sock->read_cb.read_future  = NULL;
    sock->write_cb.write_future = NULL;
}
```

## Reactor 回调与 Socket 的集成

Reactor `ms_reactor_poll` 返回就绪事件后，EventLoop 通过 `user_data`（注册时传入 `sock`）找到对应 socket，读取其内嵌回调：

```c
// src/event_loop.c — reactor 轮询处理
for (int i = 0; i < count; i++) {
    MsObjSocket* sock = (MsObjSocket*)events[i].user_data;
    if ((events[i].events & MS_IO_READABLE) && sock->read_cb.read_future)
        do_socket_recv(loop->vm, sock);
    if ((events[i].events & MS_IO_WRITABLE) && sock->write_cb.write_future)
        do_socket_send(loop->vm, sock);
    if (events[i].events & MS_IO_ERROR) {
        MsValue err = ms_string_val_from_errno(loop->vm);
        if (sock->read_cb.read_future)
            ms_future_reject(loop->vm, sock->read_cb.read_future, err);
        if (sock->write_cb.write_future)
            ms_future_reject(loop->vm, sock->write_cb.write_future, err);
    }
}
```

`user_data` 直接传入 socket 对象（GC 对象），reactor 不持有独立堆分配；GC trace socket 即可覆盖全部 future 引用。`vm->io_callbacks` 数组不再需要。

## Windows 差异

- `fd` 改为 `SOCKET`（typedef `uintptr_t`），cast 存入 `MsObjSocket.fd`
- `SOCK_NONBLOCK` 不存在：用 `ioctlsocket(fd, FIONBIO, &mode=1)` 设置非阻塞
- `EINPROGRESS` → `WSAEWOULDBLOCK`
- IOCP 路径：`ConnectEx`（需 `WSAIoctl` 取函数指针）替代 `connect`；  
  `AcceptEx` 替代 `accept`；`WSARecv`/`WSASend` + overlapped 替代 `recv`/`send`

## 注册到 VM（`src/vm_natives.c` 初始化）

```c
ms_define_native(vm, "tcp_connect", native_tcp_connect, 2);
ms_define_native(vm, "tcp_listen",  native_tcp_listen,  1);
ms_define_native(vm, "run_until_complete", native_run_until_complete, 1);
ms_define_native(vm, "gather",      native_gather,       1);
ms_define_native(vm, "sleep",       native_sleep,        1);
// socket 方法挂载到 ObjSocket 的 shape/方法表
```

## 完整用例（`tests/fixtures/async_tcp_echo.ms`）

```ms
async fun handle_client(sock) {
    while true {
        var data = await sock.read(1024)
        if data.len() == 0 { break }  // EOF：recv 返回 0，resolve 为空字符串
        await sock.write(data)
    }
    sock.close()
}

async fun server() {
    var srv = await tcp_listen(9999)
    var i = 0
    while i < 5 {
        var client = await srv.accept()
        spawn handle_client(client)
        i = i + 1
    }
}

async fun client_test() {
    await sleep(10)
    var c = await tcp_connect("127.0.0.1", 9999)
    await c.write("hello")
    var resp = await c.read(64)
    print(resp)   // 应输出 "hello"
    c.close()
}

run_until_complete(gather([server(), client_test()]))
```

## 验证

```bash
cmake --build build && cd build
ctest -R test_reactor --output-on-failure
ctest -R test_future --output-on-failure
ctest -R test_event_loop --output-on-failure
./mslang-c ../tests/fixtures/async_tcp_echo.ms
```

- echo 测试：1000 条消息往返，结果全部正确
- 无内存泄漏：`MSLANG_VM_STATS` 下 `live_objects_after_final_gc` 为 0（或仅剩全局对象）
- conformance 100% 通过（async 不影响现有同步代码）
