/* tests/unit/test_stdlib_path.c - STDLIB-29: path module */
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
        fprintf(stderr, "FAIL test_stdlib_path.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_path.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- path.sep ---- */
static void test_sep(void) {
    RUN_OK(
        "import \"path\"\n"
        "assert(path.sep == \"/\" or path.sep == \"\\\\\")\n"
    );
}

/* ---- path.basename ---- */
static void test_basename(void) {
    RUN_OK(
        "import \"path\"\n"
        "assert(path.basename(\"foo/bar/baz.txt\") == \"baz.txt\")\n"
        "assert(path.basename(\"foo/bar/\") == \"bar\")\n"
        "assert(path.basename(\"baz.txt\") == \"baz.txt\")\n"
        "assert(path.basename(\"/etc/hosts\") == \"hosts\")\n"
    );
}

/* ---- path.dirname ---- */
static void test_dirname(void) {
    RUN_OK(
        "import \"path\"\n"
        "assert(path.dirname(\"foo/bar/baz.txt\") == \"foo/bar\")\n"
        "assert(path.dirname(\"/etc/hosts\") == \"/etc\")\n"
        "assert(path.dirname(\"baz.txt\") == \".\")\n"
    );
}

/* ---- path.ext ---- */
static void test_ext(void) {
    RUN_OK(
        "import \"path\"\n"
        "assert(path.ext(\"foo/bar/baz.txt\") == \".txt\")\n"
        "assert(path.ext(\"foo/bar/baz\")     == \"\")\n"
        "assert(path.ext(\"foo.tar.gz\")      == \".gz\")\n"
        "assert(path.ext(\"/etc/hosts\")      == \"\")\n"
    );
}

/* ---- path.stem ---- */
static void test_stem(void) {
    RUN_OK(
        "import \"path\"\n"
        "assert(path.stem(\"foo/bar/baz.txt\") == \"baz\")\n"
        "assert(path.stem(\"foo/bar/baz\")     == \"baz\")\n"
        "assert(path.stem(\"foo.tar.gz\")      == \"foo.tar\")\n"
    );
}

/* ---- path.split ---- */
static void test_split(void) {
    RUN_OK(
        "import \"path\"\n"
        "var s = path.split(\"foo/bar/baz.txt\")\n"
        "assert(s[0] == \"foo/bar\")\n"
        "assert(s[1] == \"baz.txt\")\n"
    );
}

/* ---- path.split_ext ---- */
static void test_split_ext(void) {
    RUN_OK(
        "import \"path\"\n"
        "var s = path.split_ext(\"foo/bar/baz.txt\")\n"
        "assert(s[0] == \"foo/bar/baz\")\n"
        "assert(s[1] == \".txt\")\n"
    );
    RUN_OK(
        "import \"path\"\n"
        "var s = path.split_ext(\"foo/bar\")\n"
        "assert(s[0] == \"foo/bar\")\n"
        "assert(s[1] == \"\")\n"
    );
}

/* ---- path.parts ---- */
static void test_parts(void) {
    RUN_OK(
        "import \"path\"\n"
        "var p = path.parts(\"/usr/local/bin\")\n"
        "assert(p[0] == \"usr\")\n"
        "assert(p[1] == \"local\")\n"
        "assert(p[2] == \"bin\")\n"
        "assert(p.len() == 3)\n"
    );
    RUN_OK(
        "import \"path\"\n"
        "var p = path.parts(\"a//b//c\")\n"
        "assert(p.len() == 3)\n"
        "assert(p[0] == \"a\")\n"
        "assert(p[1] == \"b\")\n"
        "assert(p[2] == \"c\")\n"
    );
}

/* ---- path.join ---- */
static void test_join(void) {
    RUN_OK(
        "import \"path\"\n"
        "var j = path.join(\"foo\", \"bar\", \"baz.txt\")\n"
        "var sep = path.sep\n"
        "assert(j == \"foo\" + sep + \"bar\" + sep + \"baz.txt\")\n"
    );
    RUN_OK(
        "import \"path\"\n"
        "assert(path.join() == \"\")\n"
    );
}

/* ---- path.normalize ---- */
static void test_normalize(void) {
    RUN_OK(
        "import \"path\"\n"
        "var n = path.normalize(\"foo//bar/../baz\")\n"
        "assert(n == \"foo/baz\" or n == \"foo\\\\baz\")\n"
    );
    RUN_OK(
        "import \"path\"\n"
        "var n = path.normalize(\"./foo/./bar\")\n"
        "assert(n == \"foo/bar\" or n == \"foo\\\\bar\")\n"
    );
    RUN_OK(
        "import \"path\"\n"
        "assert(path.normalize(\"\") == \".\")\n"
        "assert(path.normalize(\".\") == \".\")\n"
    );
}

/* ---- path.is_abs / path.is_rel ---- */
static void test_is_abs_rel(void) {
    RUN_OK(
        "import \"path\"\n"
        "assert(path.is_abs(\"/etc/hosts\") == true)\n"
        "assert(path.is_abs(\"relative/path\") == false)\n"
        "assert(path.is_rel(\"relative/path\") == true)\n"
        "assert(path.is_rel(\"/etc/hosts\") == false)\n"
    );
}

/* ---- path.absolute ---- */
static void test_absolute(void) {
    RUN_OK(
        "import \"path\"\n"
        "var a = path.absolute(\".\")\n"
        "assert(path.is_abs(a))\n"
    );
    RUN_OK(
        "import \"path\"\n"
        "var a = path.absolute(\"/already/abs\")\n"
        "assert(a == \"/already/abs\" or path.is_abs(a))\n"
    );
}

/* ---- path.relative ---- */
static void test_relative(void) {
    RUN_OK(
        "import \"path\"\n"
        "var r = path.relative(\"/a/b/c\", \"/a\")\n"
        "assert(r == \"b/c\" or r == \"b\\\\c\")\n"
    );
    RUN_OK(
        "import \"path\"\n"
        "var r = path.relative(\"/a/b\", \"/a/c\")\n"
        /* up one, then b */
        "assert(r == \"../b\" or r == \"..\\\\b\")\n"
    );
    RUN_OK(
        "import \"path\"\n"
        "var r = path.relative(\"/a/b\", \"/a/b\")\n"
        "assert(r == \".\")\n"
    );
}

/* ---- type errors ---- */
static void test_type_errors(void) {
    RUN_ERR("import \"path\"\npath.dirname(42)\n");
    RUN_ERR("import \"path\"\npath.join(\"a\", 42)\n");
    RUN_ERR("import \"path\"\npath.is_abs(nil)\n");
}

int main(void) {
    test_sep();
    test_basename();
    test_dirname();
    test_ext();
    test_stem();
    test_split();
    test_split_ext();
    test_parts();
    test_join();
    test_normalize();
    test_is_abs_rel();
    test_absolute();
    test_relative();
    test_type_errors();
    printf("test_stdlib_path: all tests passed\n");
    return 0;
}
