/* tests/unit/test_stdlib_net.c - STDLIB-08: net module */
#include "../test_assert.h"
#include "ms/vm.h"
#include <stdio.h>
#include <stdlib.h>

static MsInterpretResult run_snippet(const char* src) {
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) { fprintf(stderr, "OOM\n"); exit(1); }
    ms_vm_init(vm);
    MsInterpretResult r = ms_vm_interpret(vm, src, "<test>");
    ms_vm_free(vm);
    free(vm);
    return r;
}

#define RUN_OK(src)  TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK)
#define RUN_ERR(src) TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_RUNTIME_ERROR)

/* ---- 1: import succeeds ---- */

static void test_import_net(void) {
    RUN_OK("import \"net\"");
}

/* ---- 2: net.listen(host, port) -> Socket (synchronous, non-nil) ---- */

static void test_listen_returns_socket(void) {
    RUN_OK(
        "import \"net\"\n"
        "var s = net.listen(\"127.0.0.1\", 0)\n"
        "assert(s != nil)\n"
        "s.close()\n"
    );
}

/* ---- 3: net.listen with backlog arg ---- */

static void test_listen_with_backlog(void) {
    RUN_OK(
        "import \"net\"\n"
        "var s = net.listen(\"127.0.0.1\", 0, 64)\n"
        "assert(s != nil)\n"
        "s.close()\n"
    );
}

/* ---- 4: net.listen bad args errors ---- */

static void test_listen_bad_args(void) {
    RUN_ERR("import \"net\"\nnet.listen()");
    RUN_ERR("import \"net\"\nnet.listen(8080)");
    RUN_ERR("import \"net\"\nnet.listen(\"bad_addr!!\", 8080)");
}

/* ---- 5: net.resolve("localhost") returns a list with >=1 entry ---- */

static void test_resolve_localhost(void) {
    RUN_OK(
        "import \"net\"\n"
        "var ips = net.resolve(\"localhost\")\n"
        "assert(len(ips) >= 1)\n"
    );
}

/* ---- 6: net.resolve bad args errors ---- */

static void test_resolve_bad_args(void) {
    RUN_ERR("import \"net\"\nnet.resolve()");
    RUN_ERR("import \"net\"\nnet.resolve(42)");
}

/* ---- 7: net.resolve returns list of strings ---- */

static void test_resolve_returns_strings(void) {
    RUN_OK(
        "import \"net\"\n"
        "var ips = net.resolve(\"localhost\")\n"
        "assert(type(ips[0]) == \"string\")\n"
    );
}

/* ---- 8: net.tcp_pair() returns a list of 2 sockets ---- */

static void test_tcp_pair(void) {
    RUN_OK(
        "import \"net\"\n"
        "var pair = net.tcp_pair()\n"
        "assert(len(pair) == 2)\n"
        "pair[0].close()\n"
        "pair[1].close()\n"
    );
}

/* ---- 9: tcp_pair echo via module-level net.write / net.read ---- */

static void test_tcp_pair_echo(void) {
    RUN_OK(
        "import \"net\"\n"
        "import \"time\"\n"
        "async fun do_echo() {\n"
        "    var pair = net.tcp_pair()\n"
        "    var a = pair[0]\n"
        "    var b = pair[1]\n"
        "    await net.write(a, \"ping\")\n"
        "    var buf = await net.read(b, 64)\n"
        "    assert(buf.to_str() == \"ping\")\n"
        "    net.close(a)\n"
        "    net.close(b)\n"
        "}\n"
        "time.run_until_complete(do_echo())\n"
    );
}

/* ---- 10: net.close(sock) -> nil, idempotent ---- */

static void test_net_close(void) {
    RUN_OK(
        "import \"net\"\n"
        "var s = net.listen(\"127.0.0.1\", 0)\n"
        "net.close(s)\n"
        "net.close(s)\n"   /* idempotent */
    );
}

/* ---- 11: net.read returns Buffer ---- */

static void test_read_returns_buffer(void) {
    RUN_OK(
        "import \"net\"\n"
        "import \"time\"\n"
        "async fun check() {\n"
        "    var pair = net.tcp_pair()\n"
        "    var a = pair[0]\n"
        "    var b = pair[1]\n"
        "    await net.write(a, \"hello\")\n"
        "    var data = await net.read(b, 64)\n"
        "    assert(type(data) == \"object\")\n"   /* Buffer is an object */
        "    assert(data.len() == 5)\n"
        "    net.close(a)\n"
        "    net.close(b)\n"
        "}\n"
        "time.run_until_complete(check())\n"
    );
}

/* ---- 12: socket.write accepts Buffer ---- */

static void test_write_accepts_buffer(void) {
    RUN_OK(
        "import \"net\"\n"
        "import \"buffer\"\n"
        "import \"time\"\n"
        "async fun check() {\n"
        "    var pair = net.tcp_pair()\n"
        "    var a = pair[0]\n"
        "    var b = pair[1]\n"
        "    var buf = buffer.new(8)\n"
        "    buf.append(\"world\")\n"
        "    await a.write(buf)\n"
        "    var data = await b.read(64)\n"
        "    assert(data.to_str() == \"world\")\n"
        "    a.close()\n"
        "    b.close()\n"
        "}\n"
        "time.run_until_complete(check())\n"
    );
}

/* ---- 13: net.accept / net.connect via module functions ---- */

static void test_accept_connect_module_fns(void) {
    RUN_OK(
        "import \"net\"\n"
        "import \"time\"\n"
        "async fun server(srv) {\n"
        "    var conn = await net.accept(srv)\n"
        "    net.close(conn)\n"
        "    net.close(srv)\n"
        "}\n"
        "async fun client() {\n"
        "    await time.sleep(5)\n"
        "    var c = await net.connect(\"127.0.0.1\", 19877)\n"
        "    net.close(c)\n"
        "}\n"
        "var srv = net.listen(\"127.0.0.1\", 19877)\n"
        "time.run_until_complete(time.gather([server(srv), client()]))\n"
    );
}

/* ---- main ---- */

int main(void) {
    test_import_net();
    test_listen_returns_socket();
    test_listen_with_backlog();
    test_listen_bad_args();
    test_resolve_localhost();
    test_resolve_bad_args();
    test_resolve_returns_strings();
    test_tcp_pair();
    test_tcp_pair_echo();
    test_net_close();
    test_read_returns_buffer();
    test_write_accepts_buffer();
    test_accept_connect_module_fns();
    printf("All net module tests passed.\n");
    return 0;
}
