async fun server() {
    var srv = await tcp_listen(9999)
    var client = await srv.accept()
    client.close()
    srv.close()
}
async fun client_test() {
    await sleep(10)
    var c = await tcp_connect("127.0.0.1", 9999)
    var data = await c.read(64)
    print(len(data))
    print("eof")
    c.close()
}
run_until_complete(gather([server(), client_test()]))
// expect: 0
// expect: eof
