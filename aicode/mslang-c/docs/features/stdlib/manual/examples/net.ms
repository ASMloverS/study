import "net"
import "time"

// tcp_pair：本地回环，无需外部网络的测试用 Socket 对
var pair = net.tcp_pair()
var cli = pair[0]
var srv = pair[1]

async fun main() {
    // 客户端写 → ���务端读
    await cli.write("hello mslang")
    var recv = await srv.read(12)
    print(recv.to_str())    // hello mslang

    // 服务端回写 → 客户端读
    await srv.write("pong")
    var reply = await cli.read(4)
    print(reply.to_str())   // pong

    cli.close()
    srv.close()

    // resolve（DNS 解析，需要网络；此处仅演示 API，结果因环境而异）
    // var addrs = net.resolve("localhost")
    // print(addrs[0])   // 127.0.0.1 或 ::1
}

time.run_until_complete(main())
print("net example done")   // net example done
