#include "test_assert.h"
#include "ms/vm.h"
#include <stdio.h>
#include <string.h>

/* ---- stdout capture helpers ---- */

#ifdef _WIN32
#  define _CRT_SECURE_NO_WARNINGS
#  include <io.h>
#  include <fcntl.h>
#  define DUP(fd)         _dup(fd)
#  define DUP2(src, dst)  _dup2((src), (dst))
#  define FILENO(f)       _fileno(f)
#  define TMPPATH "test_vm_closures_out.tmp"
static FILE* open_tmp(void) {
    FILE* f = NULL;
    fopen_s(&f, TMPPATH, "w+b");
    return f;
}
static void  close_tmp(FILE* f) { fclose(f); remove(TMPPATH); }
#else
#  include <unistd.h>
#  define DUP(fd)         dup(fd)
#  define DUP2(src, dst)  dup2((src), (dst))
#  define FILENO(f)       fileno(f)
static FILE* open_tmp(void) { return tmpfile(); }
static void  close_tmp(FILE* f) { fclose(f); }
#endif

/* Run src, capture stdout, return result. buf must be >= bufsz bytes. */
static MsInterpretResult run_capture(const char* src, char* buf, int bufsz) {
    fflush(stdout);
    int saved = DUP(FILENO(stdout));
    FILE* tmp = open_tmp();
    fflush(stdout);
#ifdef _WIN32
    _dup2(_fileno(tmp), _fileno(stdout));
#else
    dup2(fileno(tmp), fileno(stdout));
#endif

    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, src, "<test>");
    ms_vm_free(&vm);

    fflush(stdout);
#ifdef _WIN32
    _dup2(saved, _fileno(stdout));
    _close(saved);
#else
    dup2(saved, fileno(stdout));
    close(saved);
#endif

    rewind(tmp);
    int n = (int)fread(buf, 1, bufsz - 1, tmp);
    buf[n] = '\0';
    close_tmp(tmp);
    return r;
}

/* ---- tests ---- */

static void test_closure_captures_local(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun make() { var x = 10\n return fun() { return x } }\n"
        "print(make()())",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "10\n");
}

static void test_closure_counter(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun make_counter() {\n"
        "  var count = 0\n"
        "  fun increment() { count = count + 1\n return count }\n"
        "  return increment\n"
        "}\n"
        "var c = make_counter()\n"
        "print(c())\n"
        "print(c())\n"
        "print(c())",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "1\n2\n3\n");
}

static void test_closure_adder(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "fun make_adder(n) { return fun(x) { return x + n } }\n"
        "var add5 = make_adder(5)\n"
        "print(add5(3))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "8\n");
}

static void test_closure_independent(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "fun make_adder(n) { return fun(x) { return x + n } }\n"
        "var add5  = make_adder(5)\n"
        "var add10 = make_adder(10)\n"
        "print(add5(3))\n"
        "print(add10(3))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "8\n13\n");
}

static void test_nested_closures(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "fun outer() {\n"
        "  var x = 1\n"
        "  fun middle() {\n"
        "    var y = 2\n"
        "    fun inner() { return x + y }\n"
        "    return inner\n"
        "  }\n"
        "  return middle\n"
        "}\n"
        "print(outer()()())",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "3\n");
}

static void test_shared_upvalue(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "fun make() {\n"
        "  var x = 0\n"
        "  fun set(v) { x = v }\n"
        "  fun get()  { return x }\n"
        "  set(42)\n"
        "  return get\n"
        "}\n"
        "print(make()())",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "42\n");
}

static void test_upvalue_close_on_return(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "fun make() {\n"
        "  var x = 99\n"
        "  return fun() { return x }\n"
        "}\n"
        "var f = make()\n"
        "print(f())",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "99\n");
}

static void test_anon_fun_expr(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "var f = fun(a, b) { return a + b }\n"
        "print(f(3, 4))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "7\n");
}

int main(void) {
    test_closure_captures_local();
    test_closure_counter();
    test_closure_adder();
    test_closure_independent();
    test_nested_closures();
    test_shared_upvalue();
    test_upvalue_close_on_return();
    test_anon_fun_expr();
    printf("test_vm_closures: all passed\n");
    return 0;
}
