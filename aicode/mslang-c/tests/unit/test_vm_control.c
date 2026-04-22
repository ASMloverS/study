#include "test_assert.h"
#include "ms/vm.h"
#include <stdio.h>
#include <string.h>

/* ---- stdout capture helpers (same pattern as test_vm_closures.c) ---- */

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  define DUP(fd)         _dup(fd)
#  define DUP2(src, dst)  _dup2((src), (dst))
#  define FILENO(f)       _fileno(f)
#  define TMPPATH "test_vm_control_out.tmp"
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

/* ---- break tests ---- */

static void test_break_while(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "var i = 0\n"
        "while (true) {\n"
        "  if (i >= 3) break\n"
        "  print(i)\n"
        "  i = i + 1\n"
        "}",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "0\n1\n2\n");
}

static void test_break_for(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "for (var i = 0; i < 10; i = i + 1) {\n"
        "  if (i == 3) break\n"
        "  print(i)\n"
        "}",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "0\n1\n2\n");
}

/* ---- continue tests ---- */

static void test_continue_for(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "for (var i = 0; i < 5; i = i + 1) {\n"
        "  if (i == 2) continue\n"
        "  if (i == 4) continue\n"
        "  print(i)\n"
        "}",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "0\n1\n3\n");
}

static void test_continue_while(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "var i = 0\n"
        "while (i < 5) {\n"
        "  i = i + 1\n"
        "  if (i == 3) continue\n"
        "  print(i)\n"
        "}",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "1\n2\n4\n5\n");
}

/* ---- nested break (innermost loop only) ---- */

static void test_nested_break(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "for (var i = 0; i < 3; i = i + 1) {\n"
        "  for (var j = 0; j < 3; j = j + 1) {\n"
        "    if (j == 1) break\n"
        "    print(i * 10 + j)\n"
        "  }\n"
        "}",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "0\n10\n20\n");
}

/* ---- switch tests ---- */

static void test_switch_basic(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "fun classify(n) {\n"
        "  switch (n) {\n"
        "    case 1: return \"one\"\n"
        "    case 2: return \"two\"\n"
        "    case 3: return \"three\"\n"
        "    default: return \"other\"\n"
        "  }\n"
        "}\n"
        "print(classify(1))\n"
        "print(classify(2))\n"
        "print(classify(5))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "one\ntwo\nother\n");
}

/* ---- lambda / anonymous function tests ---- */

static void test_lambda_basic(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "var double_it = fun(x) { return x * 2 }\n"
        "print(double_it(5))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "10\n");
}

static void test_lambda_as_argument(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "fun apply(f, x) { return f(x) }\n"
        "print(apply(fun(n) { return n + 100 }, 42))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "142\n");
}

static void test_lambda_higher_order(void) {
    char buf[32];
    MsInterpretResult r = run_capture(
        "fun make_multiplier(factor) {\n"
        "  return fun(x) { return x * factor }\n"
        "}\n"
        "var triple = make_multiplier(3)\n"
        "print(triple(7))",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "21\n");
}

/* break inside switch case exits only the switch; outer while keeps running */
static void test_break_in_switch_outer_while_continues(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "var i = 0\n"
        "while (i < 3) {\n"
        "  switch (i) {\n"
        "    case 1: break\n"
        "    default: print(i)\n"
        "  }\n"
        "  i = i + 1\n"
        "}",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "0\n2\n");
}

/* continue inside nested for affects only the inner loop */
static void test_continue_inner_for_only(void) {
    char buf[64];
    MsInterpretResult r = run_capture(
        "for (var i = 0; i < 3; i = i + 1) {\n"
        "  for (var j = 0; j < 3; j = j + 1) {\n"
        "    if (j == 1) continue\n"
        "    print(i * 10 + j)\n"
        "  }\n"
        "}",
        buf, sizeof(buf));
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    TEST_ASSERT_STR_EQ(buf, "0\n2\n10\n12\n20\n22\n");
}

int main(void) {
    test_break_while();
    test_break_for();
    test_continue_for();
    test_continue_while();
    test_nested_break();
    test_switch_basic();
    test_lambda_basic();
    test_lambda_as_argument();
    test_lambda_higher_order();
    test_break_in_switch_outer_while_continues();
    test_continue_inner_for_only();
    printf("test_vm_control: all passed\n");
    return 0;
}
