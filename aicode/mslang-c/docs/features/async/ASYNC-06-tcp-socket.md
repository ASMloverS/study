# ASYNC-06: TCP Socket 绑定

## ObjSocket（`include/ms/object.h`, `src/object.c`）

### 定义

```c
typedef struct {
    MsObject obj;    // GC header，type = MS_OBJ_SOCKET
    int fd;          // POSIX fd；Windows 用 SOCKET（uintptr_t）转存
    bool connected;
    bool listening;
    bool closed;
} MsObjSocket;
```

### 生命周期

```c
MsObjSocket* ms_obj_socket_new(MsVM* vm, int fd);
// print: <Socket fd=N connected/listening/closed>
// free: 若未关闭则 close(fd)/closesocket(fd)
// gc mark: 无额外引用
```

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
// 创建 server socket，bind, listen（均同步，快速）
// 立即 resolve future（无需等待 IO）
// 返回的 Socket 对象用于后续 accept
```

### `socket.accept() -> Future<Socket>`

```c
// 向 reactor 注册 server_fd READABLE 事件
// 就绪时 accept(server_fd) → 新 client fd（非阻塞）
// resolve future，值为新 ObjSocket
```

### `socket.read(n) -> Future<bytes>`

```c
// 向 reactor 注册 fd READABLE 事件
// 就绪时 recv(fd, buf, n, 0)
// resolve future，值为 ObjString（raw bytes）
// 若 recv == 0：EOF，resolve 为 nil；< 0：reject
```

### `socket.write(bytes) -> Future<int>`

```c
// 尝试 send(fd, data, len, 0)
// 若 EAGAIN/EWOULDBLOCK：注册 WRITABLE 事件，就绪时重试
// resolve future，值为实际写入字节数
```

### `socket.close() -> nil`（同步）

```c
// close(fd) / closesocket(fd)
// reactor_unregister(fd)
// sock->closed = true
```

## Callback 结构

```c
typedef struct {
    MsObjFuture* read_future;   // READABLE 事件对应的 future
    MsObjFuture* write_future;  // WRITABLE 事件对应的 future
    MsObjSocket* socket;        // 所属 socket 对象（GC 根）
    int connect_port;           // tcp_connect 路径用
    MsCallbackType type;        // CONNECT / ACCEPT / READ / WRITE
} MsIOCallback;
```

`MsIOCallback` 生命周期由 reactor 管理；GC 需 mark `read_future`、`write_future`、`socket`。  
建议放入 `vm->io_callbacks` 动态数组（类似 `vm->remembered_set`），作为 GC 额外根集。

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
        if data == nil { break }
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
