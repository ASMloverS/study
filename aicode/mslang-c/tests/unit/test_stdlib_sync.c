/* tests/unit/test_stdlib_sync.c - STDLIB-41: sync module */
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

static void check_ok(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_OK) {
        fprintf(stderr, "FAIL test_stdlib_sync.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

/* ---- Once ---- */
static void test_once(void) {
    RUN_OK(
        "import \"sync\"\n"
        "var count = 0\n"
        "var o = sync.new_once(fun() { count = count + 1 })\n"
        "o.do()\n"
        "o.do()\n"
        "o.do()\n"
        "assert(count == 1)\n"
        "assert(o.done())\n"
    );
    RUN_OK(
        "import \"sync\"\n"
        "var o = sync.new_once(fun() {})\n"
        "assert(!o.done())\n"
        "o.do()\n"
        "assert(o.done())\n"
    );
}

/* ---- WaitGroup ---- */
static void test_wait_group_zero(void) {
    /* count already 0 → wait() resolves immediately */
    RUN_OK(
        "import \"sync\"\n"
        "import \"time\"\n"
        "var wg = sync.new_wait_group()\n"
        "var f = wg.wait()\n"
        "var result = time.run_until_complete(f)\n"
        "assert(result == nil)\n"
    );
}

static void test_wait_group_done(void) {
    RUN_OK(
        "import \"sync\"\n"
        "import \"time\"\n"
        "async fun main() {\n"
        "    var wg = sync.new_wait_group()\n"
        "    wg.add(2)\n"
        "    wg.done()\n"
        "    wg.done()\n"
        "    await wg.wait()\n"
        "    print(\"wg_ok\")\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
}

/* ---- Channel ---- */
static void test_channel_buffered(void) {
    /* Buffered channel: send/recv complete without parking */
    RUN_OK(
        "import \"sync\"\n"
        "import \"time\"\n"
        "async fun main() {\n"
        "    var ch = sync.new_channel(3)\n"
        "    await ch.send(10)\n"
        "    await ch.send(20)\n"
        "    var a = await ch.recv()\n"
        "    var b = await ch.recv()\n"
        "    assert(a == 10)\n"
        "    assert(b == 20)\n"
        "    assert(ch.len() == 0)\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
}

static void test_channel_close(void) {
    RUN_OK(
        "import \"sync\"\n"
        "import \"time\"\n"
        "async fun main() {\n"
        "    var ch = sync.new_channel(1)\n"
        "    assert(!ch.is_closed())\n"
        "    ch.close()\n"
        "    assert(ch.is_closed())\n"
        "    var v = await ch.recv()\n"
        "    assert(v == nil)\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
}

static void test_channel_handoff(void) {
    /* Unbuffered channel: sender parks until receiver arrives and vice versa */
    RUN_OK(
        "import \"sync\"\n"
        "import \"time\"\n"
        "async fun producer(ch) {\n"
        "    await ch.send(42)\n"
        "}\n"
        "async fun consumer(ch, wg) {\n"
        "    var v = await ch.recv()\n"
        "    assert(v == 42)\n"
        "    wg.done()\n"
        "}\n"
        "async fun main() {\n"
        "    var ch = sync.new_channel(0)\n"
        "    var wg = sync.new_wait_group()\n"
        "    wg.add(1)\n"
        "    var pf = producer(ch)\n"
        "    var cf = consumer(ch, wg)\n"
        "    await sync.gather([pf, cf])\n"
        "    await wg.wait()\n"
        "    print(\"handoff_ok\")\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
}

/* ---- gather: delegates to time.gather ---- */
static void test_gather(void) {
    RUN_OK(
        "import \"sync\"\n"
        "import \"time\"\n"
        "async fun main() {\n"
        "    async fun val(x) { return x }\n"
        "    var results = await sync.gather([val(1), val(2), val(3)])\n"
        "    assert(results.len() == 3)\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
}

/* ---- race ---- */
static void test_race(void) {
    RUN_OK(
        "import \"sync\"\n"
        "import \"time\"\n"
        "async fun main() {\n"
        "    var fast = time.sleep(5)\n"
        "    var slow = time.sleep(50)\n"
        "    var r = await sync.race([fast, slow])\n"
        "    print(\"race_ok\")\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
    /* race with empty list → nil */
    RUN_OK(
        "import \"sync\"\n"
        "import \"time\"\n"
        "async fun main() {\n"
        "    var r = await sync.race([])\n"
        "    assert(r == nil)\n"
        "}\n"
        "time.run_until_complete(main())\n"
    );
}

int main(void) {
    test_once();
    test_wait_group_zero();
    test_wait_group_done();
    test_channel_buffered();
    test_channel_close();
    test_channel_handoff();
    test_gather();
    test_race();
    printf("test_stdlib_sync: all passed\n");
    return 0;
}
