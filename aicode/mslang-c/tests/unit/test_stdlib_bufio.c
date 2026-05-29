/* tests/unit/test_stdlib_bufio.c - STDLIB-30: bufio module */
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
        fprintf(stderr, "FAIL test_stdlib_bufio.c:%d: expected OK (got %d)\n", line, r);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_bufio.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- bufio.read_lines / write_lines (convenience) ---- */

static void test_read_write_lines(void) {
    /* write then read back */
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_test__.txt\", \"alpha\\nbeta\\ngamma\")\n"
        "var lines = bufio.read_lines(\"__bufio_test__.txt\")\n"
        "assert(lines.len() == 3)\n"
        "assert(lines[0] == \"alpha\")\n"
        "assert(lines[1] == \"beta\")\n"
        "assert(lines[2] == \"gamma\")\n"
    );

    /* write_lines roundtrip */
    RUN_OK(
        "import \"bufio\"\n"
        "bufio.write_lines(\"__bufio_test2__.txt\", [\"x\", \"y\", \"z\"])\n"
        "var ls = bufio.read_lines(\"__bufio_test2__.txt\")\n"
        "assert(ls.len() == 3)\n"
        "assert(ls[0] == \"x\")\n"
        "assert(ls[1] == \"y\")\n"
        "assert(ls[2] == \"z\")\n"
    );

    /* empty file */
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_empty__.txt\", \"\")\n"
        "var lines = bufio.read_lines(\"__bufio_empty__.txt\")\n"
        "assert(lines.len() == 0)\n"
    );
}

/* ---- BufReader.read_line ---- */

static void test_buf_reader_read_line(void) {
    /* basic line-by-line */
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_rl__.txt\", \"line1\\nline2\\nline3\\n\")\n"
        "var f = io.open(\"__bufio_rl__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f)\n"
        "assert(br.read_line() == \"line1\")\n"
        "assert(br.read_line() == \"line2\")\n"
        "assert(br.read_line() == \"line3\")\n"
        "assert(br.read_line() == nil)\n"
        "f.close()\n"
    );

    /* last line without trailing newline */
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_nonl__.txt\", \"hello\\nworld\")\n"
        "var f = io.open(\"__bufio_nonl__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f)\n"
        "assert(br.read_line() == \"hello\")\n"
        "assert(br.read_line() == \"world\")\n"
        "assert(br.read_line() == nil)\n"
        "f.close()\n"
    );

    /* small buffer forces cross-boundary reads */
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_small__.txt\", \"abcdef\\nghijkl\\n\")\n"
        "var f = io.open(\"__bufio_small__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f, 4)\n"
        "assert(br.read_line() == \"abcdef\")\n"
        "assert(br.read_line() == \"ghijkl\")\n"
        "assert(br.read_line() == nil)\n"
        "f.close()\n"
    );

    /* empty file → nil immediately */
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_emp2__.txt\", \"\")\n"
        "var f = io.open(\"__bufio_emp2__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f)\n"
        "assert(br.read_line() == nil)\n"
        "f.close()\n"
    );
}

/* ---- BufReader.read_lines ---- */

static void test_buf_reader_read_lines(void) {
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_rls__.txt\", \"a\\nb\\nc\\n\")\n"
        "var f = io.open(\"__bufio_rls__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f)\n"
        "var lines = br.read_lines()\n"
        "assert(lines.len() == 3)\n"
        "assert(lines[0] == \"a\")\n"
        "assert(lines[2] == \"c\")\n"
        "f.close()\n"
    );
}

/* ---- BufReader.read_chunk ---- */

static void test_buf_reader_read_chunk(void) {
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_chunk__.txt\", \"ABCDEFGH\")\n"
        "var f = io.open(\"__bufio_chunk__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f)\n"
        "var c1 = br.read_chunk(4)\n"
        "var c2 = br.read_chunk(4)\n"
        "var c3 = br.read_chunk(4)\n"
        "assert(c1.len() == 4)\n"
        "assert(c2.len() == 4)\n"
        "assert(c3.len() == 0)\n"
        "f.close()\n"
    );
}

/* ---- BufReader.read_all ---- */

static void test_buf_reader_read_all(void) {
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_all__.txt\", \"hello world\")\n"
        "var f = io.open(\"__bufio_all__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f)\n"
        "var b = br.read_all()\n"
        "assert(b.len() == 11)\n"
        "f.close()\n"
    );
}

/* ---- BufReader.peek ---- */

static void test_buf_reader_peek(void) {
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_peek__.txt\", \"PEEK\")\n"
        "var f = io.open(\"__bufio_peek__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f)\n"
        "var p1 = br.peek(2)\n"
        "var p2 = br.peek(2)\n"
        /* Both peeks should return same bytes (position unchanged) */
        "assert(p1.len() == 2)\n"
        "assert(p2.len() == 2)\n"
        "f.close()\n"
    );
}

/* ---- BufWriter.write / write_line / flush ---- */

static void test_buf_writer(void) {
    /* BufWriter buffers data; flush writes to file */
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "var f = io.open(\"__bufio_wr__.txt\", \"w\")\n"
        "var bw = bufio.new_writer(f)\n"
        "bw.write_line(\"row1\")\n"
        "bw.write_line(\"row2\")\n"
        "bw.flush()\n"
        "f.close()\n"
        "var lines = bufio.read_lines(\"__bufio_wr__.txt\")\n"
        "assert(lines.len() == 2)\n"
        "assert(lines[0] == \"row1\")\n"
        "assert(lines[1] == \"row2\")\n"
    );

    /* write accepts string */
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "var f = io.open(\"__bufio_ws__.txt\", \"w\")\n"
        "var bw = bufio.new_writer(f)\n"
        "bw.write(\"hello\")\n"
        "bw.flush()\n"
        "f.close()\n"
        "var s = io.read_file(\"__bufio_ws__.txt\")\n"
        "assert(s == \"hello\")\n"
    );
}

/* ---- read_until ---- */

static void test_buf_reader_read_until(void) {
    RUN_OK(
        "import \"io\"\n"
        "import \"bufio\"\n"
        "io.write_file(\"__bufio_until__.txt\", \"key:value\\n\")\n"
        "var f = io.open(\"__bufio_until__.txt\", \"r\")\n"
        "var br = bufio.new_reader(f)\n"
        "var tok = br.read_until(\":\")\n"
        "assert(tok.len() == 4)\n" /* "key:" */
        "f.close()\n"
    );
}

/* ---- type error: non-File arg to new_reader ---- */

static void test_type_errors(void) {
    RUN_ERR("import \"bufio\"\nbufio.new_reader(42)\n");
    RUN_ERR("import \"bufio\"\nbufio.new_writer(\"not a file\")\n");
    RUN_ERR("import \"bufio\"\nbufio.read_lines(42)\n");
    RUN_ERR("import \"bufio\"\nbufio.write_lines(\"f.txt\", \"not a list\")\n");
}

int main(void) {
    test_read_write_lines();
    test_buf_reader_read_line();
    test_buf_reader_read_lines();
    test_buf_reader_read_chunk();
    test_buf_reader_read_all();
    test_buf_reader_peek();
    test_buf_writer();
    test_buf_reader_read_until();
    test_type_errors();
    printf("test_stdlib_bufio: all tests passed\n");
    return 0;
}
