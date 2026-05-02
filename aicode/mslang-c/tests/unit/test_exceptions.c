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
#  define TMPPATH "test_exceptions_out.tmp"
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

/* ---- exception tests ---- */

static void test_try_catch_basic(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "try { throw \"oops\" } catch (e) { print(e) }",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "oops\n");
}

static void test_try_no_throw(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "try { print(\"ok\") } catch (e) { print(\"error\") }",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "ok\n");
}

static void test_throw_across_frames(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun dangerous() { throw \"boom\" }\n"
        "try { dangerous() } catch (e) { print(e) }",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "boom\n");
}

static void test_try_catch_nested(void) {
    char buf[128];
    MsInterpretResult r = run_capture(
        "try {\n"
        "  try { throw \"inner\" } catch (e) {\n"
        "    print(\"caught: \" + e)\n"
        "    throw \"rethrown\"\n"
        "  }\n"
        "} catch (e2) { print(\"outer: \" + e2) }",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "caught: inner\nouter: rethrown\n");
}

static void test_uncaught_exception(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, "throw \"oops\"", "<test>");
    ms_vm_free(&vm);
    TEST_ASSERT_EQ(r, MS_INTERPRET_RUNTIME_ERROR);
}

/* ---- defer tests ---- */

static void test_defer_basic(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun test() {\n"
        "  defer fun() { print(\"deferred\") }\n"
        "  print(\"first\")\n"
        "}\n"
        "test()",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "first\ndeferred\n");
}

static void test_defer_lifo_order(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun test() {\n"
        "  defer fun() { print(\"A\") }\n"
        "  defer fun() { print(\"B\") }\n"
        "  defer fun() { print(\"C\") }\n"
        "  print(\"body\")\n"
        "}\n"
        "test()",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "body\nC\nB\nA\n");
}

static void test_defer_with_return(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun test() {\n"
        "  defer fun() { print(\"cleanup\") }\n"
        "  return 42\n"
        "}\n"
        "print(test())",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "cleanup\n42\n");
}

static void test_defer_with_throw(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun risky() {\n"
        "  defer fun() { print(\"defer ran\") }\n"
        "  throw \"fail\"\n"
        "}\n"
        "try { risky() } catch (e) { print(\"caught: \" + e) }",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "defer ran\ncaught: fail\n");
}

int main(void) {
    test_try_catch_basic();
    test_try_no_throw();
    test_throw_across_frames();
    test_try_catch_nested();
    test_uncaught_exception();
    test_defer_basic();
    test_defer_lifo_order();
    test_defer_with_return();
    test_defer_with_throw();
    printf("test_exceptions: all passed\n");
    return 0;
}
