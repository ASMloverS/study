# net 模块

TCP 网络：监听、连接、收发数据（全异步），以及 DNS 解析。

```ms
import "net"
import "time"   // 需要事件循环驱动
```

> 实现规格：[STDLIB-08-net.md](../STDLIB-08-net.md)

---

## 函数速查表

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `net.listen(host, port)` | str, int | Socket | 创建 TCP 监听 Socket（非阻塞）|
| `net.connect(host, port)` | str, int | Future\<Socket\> | 异步 TCP 连接 |
| `net.accept(sock)` | Socket | Future\<Socket\> | 等待客户端连接 |
| `net.read(sock, n)` | Socket, int | Future\<Buffer\> | 读最多 n 字节 |
| `net.write(sock, data)` | Socket, str \| Buffer | Future\<int\> | 写数据，返回写入字节数 |
| `net.close(sock)` | Socket | nil | 关闭 Socket |
| `net.resolve(host)` | str | list[str] | DNS 解析，返回 IP 地址列表 |
| `net.tcp_pair()` | — | [Socket, Socket] | 创建已连接的本地 Socket 对（测试用）|

## Socket 实例方法

Socket 对象也支持方法调用形式（等价于上方的模块函数）：

| 方法 | 等价于 | 说明 |
|---|---|---|
| `srv.accept()` | `net.accept(srv)` | 等待客户端连接 |
| `sock.read(n)` | `net.read(sock, n)` | 读最多 n 字节 |
| `sock.write(data)` | `net.write(sock, data)` | 写数据 |
| `sock.close()` | `net.close(sock)` | 关闭 |

---

## 分组详解

### tcp_pair（测试/演示专用）

`net.tcp_pair()` 返回两端已连接的 Socket，无需绑定端口，适合单元测试和示例：

```ms
import "net"
import "time"

var pair = net.tcp_pair()
var cli = pair[0]
var srv = pair[1]

async fun main() {
    await cli.write("ping")
    var r = await srv.read(4)
    print(r.to_str())   // ping
    cli.close()
    srv.close()
}
time.run_until_complete(main())
```

### TCP 服务端 / 客户端

```ms
import "net"
import "time"

async fun server() {
    var srv = net.listen("127.0.0.1", 9876)
    var conn = await srv.accept()
    var data = await conn.read(64)
    await conn.write(data)    // 回显
    conn.close()
    srv.close()
}

async fun client() {
    await time.sleep(10)      // 等服务端就绪（10 ms）
    var sock = await net.connect("127.0.0.1", 9876)
    await sock.write("hello")
    var resp = await sock.read(64)
    print(resp.to_str())      // hello
    sock.close()
}

time.run_until_complete(time.gather([server(), client()]))
```

### DNS 解析

```ms
import "net"
var addrs = net.resolve("localhost")
print(addrs[0])   // 127.0.0.1 或 ::1（因平台而异）
```

---

## 完整示例

文件：[`examples/net.ms`](examples/net.ms)

```ms
import "net"
import "time"

var pair = net.tcp_pair()
var cli = pair[0]
var srv = pair[1]

async fun main() {
    await cli.write("hello mslang")
    var recv = await srv.read(12)
    print(recv.to_str())

    await srv.write("pong")
    var reply = await cli.read(4)
    print(reply.to_str())

    cli.close()
    srv.close()
}

time.run_until_complete(main())
print("net example done")
```

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/net.ms
hello mslang
pong
net example done
```

---

## 实现/性能注解

- 所有 IO 操作（`connect` / `accept` / `read` / `write`）都是**非阻塞 + Future**，必须在 `async` 函数里 `await`，或由 `time.run_until_complete` / `time.gather` 驱动。
- `net.read(sock, n)` 读取**最多** n 字节，实际返回的 Buffer 可能少于 n（取决于 TCP 分片）。
- 直接调用 `net.read()` 等不在事件循环内执行的代码会注册到事件循环但不驱动，导致 Future 永远 pending。

## 常见陷阱

```ms
import "net"
import "time"

// ❌ 不能在同步代码��直接 await，必须在 async 函数内
// var data = await sock.read(64)  → 语法错误

// ❌ read 不保证读满 n 字节
// var r = await sock.read(100)
// print(r.len() == 100)   // 不一定成立

// ✅ 用 tcp_pair 做本地测试，避免端口冲突
var pair = net.tcp_pair()
// ... 进行测试 ...
pair[0].close()
pair[1].close()
```
