#include "test_assert.h"
#include "ms/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stdout capture helpers ---- */

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  define DUP(fd)        _dup(fd)
#  define DUP2(src,dst)  _dup2((src),(dst))
#  define FILENO(f)      _fileno(f)
#  define TMPPATH "test_natives_out.tmp"
static FILE* open_tmp(void) { FILE* f = NULL; fopen_s(&f, TMPPATH, "w+b"); return f; }
static void  close_tmp(FILE* f) { fclose(f); remove(TMPPATH); }
#else
#  include <unistd.h>
#  define DUP(fd)        dup(fd)
#  define DUP2(src,dst)  dup2((src),(dst))
#  define FILENO(f)      fileno(f)
static FILE* open_tmp(void) { return tmpfile(); }
static void  close_tmp(FILE* f) { fclose(f); }
#endif

static MsInterpretResult run_capture(const char* src, char* buf, int bufsz) {
    fflush(stdout);
    int saved = DUP(FILENO(stdout));
    FILE* tmp = open_tmp();
    DUP2(FILENO(tmp), FILENO(stdout));

    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, src, NULL);
    ms_vm_free(&vm);

    fflush(stdout);
    DUP2(saved, FILENO(stdout));
#ifdef _WIN32
    _close(saved);
#else
    close(saved);
#endif

    rewind(tmp);
    int n = (int)fread(buf, 1, (size_t)(bufsz - 1), tmp);
    buf[n < 0 ? 0 : n] = '\0';
    close_tmp(tmp);
    return r;
}

/* ---- tests ---- */

static void test_type_function(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "print(type(42))\nprint(type(3.14))\nprint(type(\"hi\"))\nprint(type(true))\nprint(type(nil))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "int")    != NULL);
    TEST_ASSERT(strstr(buf, "number") != NULL);
    TEST_ASSERT(strstr(buf, "string") != NULL);
    TEST_ASSERT(strstr(buf, "bool")   != NULL);
    TEST_ASSERT(strstr(buf, "nil")    != NULL);
}

static void test_type_collections(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "print(type([1,2]))\nprint(type({\"a\":1}))\nprint(type((1,2,3)))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "list")  != NULL);
    TEST_ASSERT(strstr(buf, "map")   != NULL);
    TEST_ASSERT(strstr(buf, "tuple") != NULL);
}

static void test_str_conversion(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "print(str(42))\nprint(str(true))\nprint(str(nil))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "42")   != NULL);
    TEST_ASSERT(strstr(buf, "true") != NULL);
    TEST_ASSERT(strstr(buf, "nil")  != NULL);
}

static void test_num_conversion(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "print(num(\"3.14\"))\nprint(num(42))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "3.14") != NULL);
    TEST_ASSERT(strstr(buf, "42")   != NULL);
}

static void test_int_conversion(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "print(int(3.7))\nprint(int(\"10\"))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "3")  != NULL);
    TEST_ASSERT(strstr(buf, "10") != NULL);
}

static void test_float_conversion(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "print(float(42))\nprint(type(float(42)))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "42")     != NULL);
    TEST_ASSERT(strstr(buf, "number") != NULL);
}

static void test_len(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "print(len(\"hello\"))\nprint(len([1,2,3]))\nprint(len((1,2,3,4)))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "5") != NULL);
    TEST_ASSERT(strstr(buf, "3") != NULL);
    TEST_ASSERT(strstr(buf, "4") != NULL);
}

static void test_clock(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "var t = clock()\nprint(type(t))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "number") != NULL);
}

static void test_assert_pass(void) {
    char buf[256];
    MsInterpretResult r = run_capture(
        "assert(true)\nassert(1 == 1)\nassert(1 < 2, \"math broken\")\nprint(\"ok\")",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "ok") != NULL);
}

static void test_assert_fail(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, "assert(false)", NULL);
    ms_vm_free(&vm);
    TEST_ASSERT_EQ(r, MS_INTERPRET_RUNTIME_ERROR);
}

static void test_print(void) {
    char buf[64];
    MsInterpretResult r = run_capture("print(\"hello\")", buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "hello") != NULL);
}

int main(void) {
    test_type_function();
    test_type_collections();
    test_str_conversion();
    test_num_conversion();
    test_int_conversion();
    test_float_conversion();
    test_len();
    test_clock();
    test_assert_pass();
    test_assert_fail();
    test_print();
    printf("test_natives: all passed\n");
    return 0;
}
