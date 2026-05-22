import "net"
import "time"
async fun server() {
    var srv = net.listen("127.0.0.1", 9998)
    var client = await srv.accept()
    client.close()
    srv.close()
}
async fun client_test() {
    await time.sleep(10)
    var c = await net.connect("127.0.0.1", 9998)
    var data = await c.read(64)
    print(len(data))
    print("eof")
    c.close()
}
time.run_until_complete(time.gather([server(), client_test()]))
// expect: 0
// expect: eof
