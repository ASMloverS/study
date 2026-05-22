/* tests/unit/test_stdlib_io.c - STDLIB-04: io module */
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

/* ---- io.write_file + io.read_file roundtrip ---- */

static void test_read_write_roundtrip(void) {
    RUN_OK(
        "import \"io\"\n"
        "io.write_file(\"__mslang_io_test__.txt\", \"hello world\")\n"
        "var s = io.read_file(\"__mslang_io_test__.txt\")\n"
        "assert(s == \"hello world\")"
    );
}

/* ---- io.lines ---- */

static void test_lines(void) {
    RUN_OK(
        "import \"io\"\n"
        "io.write_file(\"__mslang_io_test__.txt\", \"line1\\nline2\\nline3\")\n"
        "var lines = io.lines(\"__mslang_io_test__.txt\")\n"
        "assert(type(lines) == \"list\")\n"
        "assert(len(lines) == 3)\n"
        "assert(lines[0] == \"line1\")\n"
        "assert(lines[1] == \"line2\")\n"
        "assert(lines[2] == \"line3\")"
    );
}

/* ---- io.append_file ---- */

static void test_append(void) {
    RUN_OK(
        "import \"io\"\n"
        "io.write_file(\"__mslang_io_test__.txt\", \"hello\")\n"
        "io.append_file(\"__mslang_io_test__.txt\", \" world\")\n"
        "var s = io.read_file(\"__mslang_io_test__.txt\")\n"
        "assert(s == \"hello world\")"
    );
}

/* ---- io.open + readline + close ---- */

static void test_open_readline(void) {
    RUN_OK(
        "import \"io\"\n"
        "io.write_file(\"__mslang_io_test__.txt\", \"first\\nsecond\\n\")\n"
        "var f = io.open(\"__mslang_io_test__.txt\", \"r\")\n"
        "var l1 = f.readline()\n"
        "assert(l1 != nil)\n"
        "var l2 = f.readline()\n"
        "assert(l2 != nil)\n"
        "f.close()"
    );
}

/* ---- io.open missing file triggers runtime error ---- */

static void test_open_missing(void) {
    RUN_ERR(
        "import \"io\"\n"
        "var f = io.open(\"__no_such_file_xyz__.txt\", \"r\")"
    );
}

/* ---- io.read_file missing file triggers runtime error ---- */

static void test_read_file_missing(void) {
    RUN_ERR(
        "import \"io\"\n"
        "io.read_file(\"__no_such_file_xyz__.txt\")"
    );
}

/* ---- io.stdin / io.stdout / io.stderr exported ---- */

static void test_stdin_stdout_stderr(void) {
    /* stdin/stdout/stderr are ObjFile objects; type() returns "object" for
       object types that don't have a named case in native_type */
    RUN_OK(
        "import \"io\"\n"
        "assert(io.stdin  != nil)\n"
        "assert(io.stdout != nil)\n"
        "assert(io.stderr != nil)"
    );
}

/* ---- io.open write mode ---- */

static void test_open_write(void) {
    RUN_OK(
        "import \"io\"\n"
        "var f = io.open(\"__mslang_io_test__.txt\", \"w\")\n"
        "f.write(\"via open\")\n"
        "f.close()\n"
        "var s = io.read_file(\"__mslang_io_test__.txt\")\n"
        "assert(s == \"via open\")"
    );
}

/* ---- io.open seek + tell ---- */

static void test_open_seek_tell(void) {
    RUN_OK(
        "import \"io\"\n"
        "io.write_file(\"__mslang_io_test__.txt\", \"abcdef\")\n"
        "var f = io.open(\"__mslang_io_test__.txt\", \"r\")\n"
        "var p = f.seek(3, 0)\n"   /* SEEK_SET */
        "assert(p == 3)\n"
        "var t = f.tell()\n"
        "assert(t == 3)\n"
        "f.close()"
    );
}

int main(void) {
    test_read_write_roundtrip();
    test_lines();
    test_append();
    test_open_readline();
    test_open_missing();
    test_read_file_missing();
    test_stdin_stdout_stderr();
    test_open_write();
    test_open_seek_tell();
    printf("test_stdlib_io: all passed\n");
    return 0;
}
