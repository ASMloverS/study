# STDLIB-08: net 模块

## 职责

TCP 网络：将全局 `tcp_listen`、`tcp_connect`、`_socket_*` 迁移为 `net.*` 模块函数，并新增 `net.resolve`（DNS 查询）。底层复用已有 `ObjSocket` + reactor。

---

## 函数清单

### TCP

| 函数 | 参数 | 返回 | async | 描述 |
|---|---|---|---|---|
| `net.listen(host, port, backlog=128)` | str,int,int | Socket | – | 绑定并监听（同步）；迁自 `tcp_listen` |
| `net.connect(host, port)` | str,int | Future\<Socket\> | ✓ | 非阻塞连接；迁自 `tcp_connect` |
| `net.accept(sock)` | Socket | Future\<Socket\> | ✓ | 接受连接；迁自 `_socket_accept` |
| `net.read(sock, n)` | Socket,int | Future\<Buffer\> | ✓ | 读最多 n 字节；迁自 `_socket_read` |
| `net.write(sock, data)` | Socket,str\|Buffer | Future\<int\> | ✓ | 写入；迁自 `_socket_write` |
| `net.close(sock)` | Socket | nil | – | 迁自 `_socket_close` |

> `net.read` 返回 `Buffer` 而非原来的 `str`，与 `buffer` 模块统一。原 `_socket_read` 如果当前返回 str，在此处更改语义。

### DNS

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `net.resolve(host)` | str | list\[str\] | 同步 `getaddrinfo`，返回 IPv4/IPv6 地址字符串列表 |

### 工具

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `net.tcp_pair()` | – | list\[Socket, Socket\] | 创建已连接的 socket 对（`socketpair`/`CreatePipe` 替代），用于测试 |

### Socket 方法（`ObjSocket.invoke`，保留现有行为）

Socket 句柄上的方法（`s.read(n)`、`s.write(data)`、`s.close()`、`s.accept()`）继续由 `ms_socket_invoke`（`src/vm_builtins.c`）处理，不在此更改。模块函数是其别名，两条路径调同一底层实现。

### net.read 入口统一

**唯一入口**：通过 `ObjSocket` 方法 `s.read(n)` 调用（由 `ms_socket_invoke` 路由）。

- `s.read(n)` 返回 **Buffer**（与 buffer 模块统一）。
- `net.read(sock, n)` 作为别名保留，等价于 `sock.read(n)`，行为相同。
- `ms_socket_invoke` 中 `read` 分支同步修改为返回 `ObjBuffer`（原来返回 `ObjString`）；所有调用方需迁移。

> 不保留 "read 返 str" 的旧路径，避免双语义。

---

## 迁移实现（`src/stdlib/net.c`）

原始实现从 `src/vm_natives.c` 搬移，改为 `static` 函数。函数体**不变**，只更改定义位置。

```c
// src/stdlib/net.c
#include "ms/module.h"
#include "ms/object.h"
#include "ms/event_loop.h"
#include "ms/reactor.h"
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

/* 完整函数体从 vm_natives.c 搬移，以下是符号重命名示例 */

/* 原 native_tcp_listen → ms_net_listen（static）*/
static MsValue ms_net_listen(MsVM* vm, int argc, MsValue* argv) {
    /* ... 与原 native_tcp_listen 相同 ... */
}

/* 原 native_tcp_connect → ms_net_connect（static）*/
static MsValue ms_net_connect(MsVM* vm, int argc, MsValue* argv) {
    /* ... 与原 native_tcp_connect 相同 ... */
}

/* 原 native_socket_accept → ms_net_accept（static）*/
static MsValue ms_net_accept(MsVM* vm, int argc, MsValue* argv) {
    /* ... */
}

/* read 返回 Buffer 而非 str（语义调整）*/
static MsValue ms_net_read(MsVM* vm, int argc, MsValue* argv) {
    /* 原逻辑不变，结果改为 ms_obj_buffer_from_bytes */
}

static MsValue ms_net_write(MsVM* vm, int argc, MsValue* argv) {
    /* ... */
}

static MsValue ms_net_close(MsVM* vm, int argc, MsValue* argv) {
    /* ... */
}

/* 新增：getaddrinfo */
static MsValue ms_net_resolve(MsVM* vm, int argc, MsValue* argv) {
    const char* host = MS_AS_CSTRING(argv[0]);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        ms_vm_runtime_error(vm, "net.resolve: failed for '%s'", host);
        return MS_NIL_VAL();
    }
    MsValue list = MS_OBJ_VAL(ms_obj_list_new(vm));
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        char ip[INET6_ADDRSTRLEN];
        void* addr = (p->ai_family == AF_INET)
            ? (void*)&((struct sockaddr_in*)p->ai_addr)->sin_addr
            : (void*)&((struct sockaddr_in6*)p->ai_addr)->sin6_addr;
        inet_ntop(p->ai_family, addr, ip, sizeof(ip));
        MsValue s = MS_OBJ_VAL(ms_obj_string_copy(vm, ip, (int)strlen(ip)));
        ms_value_array_push(&MS_AS_LIST(list)->items, s);
    }
    freeaddrinfo(res);
    return list;
}

static const MsNativeDef ms_net_defs[] = {
    {"listen",   ms_net_listen,   2},  /* host, port（backlog 有默认值，用 argc 判断）*/
    {"connect",  ms_net_connect,  2},
    {"accept",   ms_net_accept,   1},
    {"read",     ms_net_read,     2},
    {"write",    ms_net_write,    2},
    {"close",    ms_net_close,    1},
    {"resolve",  ms_net_resolve,  1},
    {"tcp_pair", ms_net_tcp_pair, 0},
    {NULL, NULL, 0}
};

void ms_module_net_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_net_defs);
}
```

---

## `net.listen` 默认参数处理

ms 语言调用时 `net.listen("0.0.0.0", 8080)` 只传两个参数，backlog 需要在 C 里检查 `argc`：

```c
static MsValue ms_net_listen(MsVM* vm, int argc, MsValue* argv) {
    const char* host = MS_AS_CSTRING(argv[0]);
    int port    = (int)MS_AS_INT(argv[1]);
    int backlog = (argc >= 3) ? (int)MS_AS_INT(argv[2]) : 128;
    /* ... */
}
```

同理对 `net.read`（n 默认 4096）等。

### net.listen(host, port[, backlog]) → Socket

**同步**返回已绑定监听的 `Socket` 对象。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| host | str  | —      | 绑定地址，如 `"0.0.0.0"` 或 `"127.0.0.1"` |
| port | int  | —      | 监听端口（1–65535）|
| backlog | int | 128  | 内核连接队列长度 |

**实现要点**：
- 旧 `native_tcp_listen(port)` arity=1、host 写死 `INADDR_ANY`、返回 Future — **废弃**，不迁移，改为本函数。
- 新实现：`argc in [2,3]`；`inet_pton(AF_INET, host, ...)` 解析 host；返回 `ObjSocket`（同步，非 Future）。
- accept/read/write/close 操作通过 Future 异步执行（不影响 listen）。

> **注**：CAPI-08 §迁移前清单中 `tcp_listen, 2` 条目已过时，应标记为"已废弃，迁移至 net.listen（3 参）"。

---

## 依赖

- CAPI-01/02（注册）
- ASYNC-05/06（Reactor、ObjSocket、ms_obj_socket_new）
- CAPI-08（vm_natives.c 中移除全局注册）
- STDLIB-05（Buffer，read 返回值）

---

## 测试

```ms
// tests/fixtures/stdlib_net_server.ms
import net, time

async fun handle(conn) {
    var data = await net.read(conn, 1024)
    await net.write(conn, data)  // echo
    net.close(conn)
}

async fun main() {
    var server = net.listen("127.0.0.1", 19876)
    var client_conn = await net.accept(server)
    await handle(client_conn)
    net.close(server)
}

time.run_until_complete(main())
```

```ms
// tests/fixtures/stdlib_net_resolve.ms
import net
var ips = net.resolve("localhost")
assert(len(ips) >= 1)
print(ips[0])  // "127.0.0.1" 或 "::1"
```

```c
// tests/unit/test_stdlib_net.c
// 复用 test_tcp_socket.c 的 server+client 模式，改为 import net 调用
```
