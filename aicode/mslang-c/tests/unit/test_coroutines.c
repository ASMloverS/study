#include "test_assert.h"
#include "ms/vm.h"
#include <stdio.h>
#include <string.h>

/* ---- stdout capture helpers ---- */

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  define DUP(fd)         _dup(fd)
#  define DUP2(src, dst)  _dup2((src), (dst))
#  define FILENO(f)       _fileno(f)
#  define TMPPATH "test_coroutines_out.tmp"
static FILE* open_tmp(void) {
    FILE* f = NULL;
    fopen_s(&f, TMPPATH, "w+b");
    return f;
}
static void close_tmp(FILE* f) { fclose(f); remove(TMPPATH); }
#else
#  include <unistd.h>
#  define DUP(fd)         dup(fd)
#  define DUP2(src, dst)  dup2((src), (dst))
#  define FILENO(f)       fileno(f)
static FILE* open_tmp(void) { return tmpfile(); }
static void  close_tmp(FILE* f) { fclose(f); }
#endif

static MsInterpretResult run_capture(const char* src, char* buf, int bufsz) {
    fflush(stdout);
    int saved = DUP(FILENO(stdout));
    FILE* tmp = open_tmp();
    fflush(stdout);
    DUP2(FILENO(tmp), FILENO(stdout));

    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, src, "<test>");
    ms_vm_free(&vm);

    fflush(stdout);
    DUP2(saved, FILENO(stdout));
#ifdef _WIN32
    _close(saved);
#else
    close(saved);
#endif

    rewind(tmp);
    int n = (int)fread(buf, 1, bufsz - 1, tmp);
    buf[n] = '\0';
    close_tmp(tmp);
    return r;
}

/* ---- coroutine tests ---- */

static void test_generator_basic(void) {
    char buf[128];
    MsInterpretResult r = run_capture(
        "fun* count() { yield 1\n yield 2\n yield 3 }\n"
        "var g = count()\n"
        "print(resume(g))\nprint(resume(g))\nprint(resume(g))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "1\n2\n3\n");
}

static void test_generator_with_loop(void) {
    char buf[128];
    MsInterpretResult r = run_capture(
        "fun* count_to(n) {\n"
        "  for (var i = 1; i <= n; i = i + 1) {\n"
        "    yield i\n"
        "  }\n"
        "}\n"
        "var g = count_to(3)\n"
        "print(resume(g))\n"
        "print(resume(g))\n"
        "print(resume(g))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "1\n2\n3\n");
}

static void test_generator_infinite(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun* naturals() {\n"
        "  var n = 0\n"
        "  while (true) {\n"
        "    yield n\n"
        "    n = n + 1\n"
        "  }\n"
        "}\n"
        "var g = naturals()\n"
        "print(resume(g))\n"
        "print(resume(g))\n"
        "print(resume(g))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "0\n1\n2\n");
}

static void test_generator_send(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun* accumulator() {\n"
        "  var total = 0\n"
        "  while (true) {\n"
        "    var n = yield total\n"
        "    total = total + n\n"
        "  }\n"
        "}\n"
        "var acc = accumulator()\n"
        "resume(acc)\n"
        "print(resume(acc, 10))\n"
        "print(resume(acc, 20))\n"
        "print(resume(acc, 5))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "10\n30\n35\n");
}

static void test_multiple_generators(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun* gen(start) {\n"
        "  yield start\n"
        "  yield start + 1\n"
        "}\n"
        "var a = gen(10)\n"
        "var b = gen(20)\n"
        "print(resume(a))\n"
        "print(resume(b))\n"
        "print(resume(a))\n"
        "print(resume(b))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "10\n20\n11\n21\n");
}

int main(void) {
    test_generator_basic();
    test_generator_with_loop();
    test_generator_infinite();
    test_generator_send();
    test_multiple_generators();
    printf("test_coroutines: all passed\n");
    return 0;
}
