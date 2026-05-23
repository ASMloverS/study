import "net"
import "time"

// tcp_pair echo: module-level net.write / net.read
async fun test_echo() {
    var pair = net.tcp_pair()
    var a = pair[0]
    var b = pair[1]
    await net.write(a, "ping")
    var buf = await net.read(b, 64)
    assert(buf.to_str() == "ping")
    net.close(a)
    net.close(b)
}
time.run_until_complete(test_echo())

// tcp_pair echo: socket method .write / .read
import "buffer"
async fun test_socket_methods() {
    var pair = net.tcp_pair()
    var a = pair[0]
    var b = pair[1]
    var data = buffer.new(8)
    data.append("world")
    await a.write(data)
    var got = await b.read(64)
    assert(got.to_str() == "world")
    a.close()
    b.close()
}
time.run_until_complete(test_socket_methods())

// net.read returns a buffer-like object with .len() and .to_str()
async fun test_read_type() {
    var pair = net.tcp_pair()
    var a = pair[0]
    var b = pair[1]
    await net.write(a, "hello")
    var d = await net.read(b, 64)
    assert(d.len() == 5)
    assert(d.to_str() == "hello")
    net.close(a)
    net.close(b)
}
time.run_until_complete(test_read_type())

print("ok")
// expect: ok
