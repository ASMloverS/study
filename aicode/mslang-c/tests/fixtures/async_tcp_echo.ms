async fun handle_client(sock) {
    var data = await sock.read(64)
    await sock.write(data)
    sock.close()
}
async fun server() {
    var srv = await tcp_listen(9999)
    var client = await srv.accept()
    await handle_client(client)
    srv.close()
}
async fun client_test() {
    await sleep(10)
    var c = await tcp_connect("127.0.0.1", 9999)
    await c.write("hello")
    var resp = await c.read(64)
    print(resp)
    c.close()
}
run_until_complete(gather([server(), client_test()]))
// expect: hello
