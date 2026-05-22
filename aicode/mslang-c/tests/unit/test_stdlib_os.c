/* tests/unit/test_stdlib_os.c - STDLIB-02: os module */
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

#define RUN_OK(src)   TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_OK)
#define RUN_ERR(src)  TEST_ASSERT_EQ(run_snippet(src), MS_INTERPRET_RUNTIME_ERROR)

/* ---- process ---- */

static void test_name(void) {
    RUN_OK("import \"os\"\nvar n = os.name()\nassert(n == \"windows\" or n == \"linux\" or n == \"darwin\" or n == \"bsd\" or n == \"unknown\")");
}

static void test_pid(void) {
    RUN_OK("import \"os\"\nvar p = os.pid()\nassert(p > 0)");
}

static void test_argv(void) {
    /* argv() returns a list; when called from tests no script args are set */
    RUN_OK("import \"os\"\nvar a = os.argv()\nassert(type(a) == \"list\")");
}

/* ---- env ---- */

static void test_env_missing(void) {
    RUN_OK("import \"os\"\nassert(os.env(\"__MSLANG_NO_SUCH_VAR__\") == nil)");
}

static void test_setenv_getenv(void) {
    RUN_OK("import \"os\"\nos.setenv(\"__MSLANG_TEST_KEY__\", \"hello\")\nassert(os.env(\"__MSLANG_TEST_KEY__\") == \"hello\")");
}

static void test_unsetenv(void) {
    RUN_OK("import \"os\"\nos.setenv(\"__MSLANG_UNSET__\", \"val\")\nos.unsetenv(\"__MSLANG_UNSET__\")\nassert(os.env(\"__MSLANG_UNSET__\") == nil)");
}

static void test_environ(void) {
    RUN_OK("import \"os\"\nvar e = os.environ()\nassert(type(e) == \"map\")");
}

/* ---- filesystem ---- */

static void test_cwd(void) {
    RUN_OK("import \"os\"\nvar c = os.cwd()\nassert(type(c) == \"string\")\nassert(len(c) > 0)");
}

static void test_exists_isfile_isdir(void) {
    /* The build directory exists; test with a path we know doesn't exist */
    RUN_OK("import \"os\"\nassert(os.exists(\"__no_such_path_xyz__\") == false)");
    RUN_OK("import \"os\"\nassert(os.isfile(\"__no_such_path_xyz__\") == false)");
    RUN_OK("import \"os\"\nassert(os.isdir(\"__no_such_path_xyz__\") == false)");
}

static void test_mkdir_rmdir(void) {
    RUN_OK(
        "import \"os\"\n"
        "var d = \"__mslang_test_dir__\"\n"
        "os.mkdir(d)\n"
        "assert(os.isdir(d))\n"
        "os.rmdir(d)\n"
        "assert(os.exists(d) == false)"
    );
}

static void test_makedirs(void) {
    RUN_OK(
        "import \"os\"\n"
        "var d = \"__mslang_makedirs__/__inner__\"\n"
        "os.makedirs(d)\n"
        "assert(os.isdir(d))\n"
        "os.rmdir(d)\n"
        "os.rmdir(\"__mslang_makedirs__\")\n"
        "assert(os.exists(\"__mslang_makedirs__\") == false)"
    );
}

static void test_remove(void) {
    RUN_OK(
        "import \"os\"\n"
        /* Create a file via exec to avoid io dependency */
        "var f = \"__mslang_rm_test__.txt\"\n"
        "os.exec(\"echo x > \" + f)\n"
        "os.remove(f)\n"
        "assert(os.exists(f) == false)"
    );
}

static void test_rename(void) {
    RUN_OK(
        "import \"os\"\n"
        "os.mkdir(\"__mslang_ren_src__\")\n"
        "os.rename(\"__mslang_ren_src__\", \"__mslang_ren_dst__\")\n"
        "assert(os.exists(\"__mslang_ren_src__\") == false)\n"
        "assert(os.isdir(\"__mslang_ren_dst__\"))\n"
        "os.rmdir(\"__mslang_ren_dst__\")"
    );
}

static void test_listdir(void) {
    RUN_OK(
        "import \"os\"\n"
        "var d = \"__mslang_listdir__\"\n"
        "os.mkdir(d)\n"
        "var items = os.listdir(d)\n"
        "assert(type(items) == \"list\")\n"
        "assert(len(items) == 0)\n"
        "os.rmdir(d)"
    );
}

static void test_stat(void) {
    RUN_OK(
        "import \"os\"\n"
        "var d = \"__mslang_stat_dir__\"\n"
        "os.mkdir(d)\n"
        "var s = os.stat(d)\n"
        "assert(s[\"isdir\"] == true)\n"
        "assert(s[\"isfile\"] == false)\n"
        "os.rmdir(d)"
    );
}

static void test_realpath(void) {
    RUN_OK(
        "import \"os\"\n"
        "var d = \"__mslang_rp__\"\n"
        "os.mkdir(d)\n"
        "var rp = os.realpath(d)\n"
        "assert(type(rp) == \"string\")\n"
        "assert(len(rp) > 0)\n"
        "os.rmdir(d)"
    );
}

/* ---- path helpers ---- */

static void test_basename(void) {
    RUN_OK("import \"os\"\nassert(os.basename(\"/foo/bar/baz.txt\") == \"baz.txt\")");
    RUN_OK("import \"os\"\nassert(os.basename(\"hello\") == \"hello\")");
}

static void test_dirname(void) {
    RUN_OK("import \"os\"\nassert(os.dirname(\"/foo/bar/baz\") == \"/foo/bar\")");
    RUN_OK("import \"os\"\nassert(os.dirname(\"file.txt\") == \".\")");
}

static void test_splitext(void) {
    RUN_OK("import \"os\"\nvar t = os.splitext(\"foo.txt\")\nassert(t[0] == \"foo\")\nassert(t[1] == \".txt\")");
    RUN_OK("import \"os\"\nvar t = os.splitext(\"noext\")\nassert(t[0] == \"noext\")\nassert(t[1] == \"\")");
}

static void test_join(void) {
    /* Just verify it returns a string and doesn't crash */
    RUN_OK("import \"os\"\nvar j = os.join(\"a\", \"b\", \"c\")\nassert(type(j) == \"string\")\nassert(len(j) > 0)");
}

/* ---- constants ---- */

static void test_constants(void) {
    RUN_OK("import \"os\"\nassert(os.sep == \"/\" or os.sep == \"\\\\\")");
    RUN_OK("import \"os\"\nassert(type(os.linesep) == \"string\")");
    RUN_OK("import \"os\"\nassert(os.pathsep == \":\" or os.pathsep == \";\")");
    RUN_OK("import \"os\"\nassert(type(os.devnull) == \"string\")");
}

/* ---- exec ---- */

static void test_exec(void) {
    /* system() with a trivial no-op command; exit code varies by platform */
    RUN_OK("import \"os\"\nvar c = os.exec(\"echo hello\")\nassert(type(c) == \"int\")");
}

int main(void) {
    test_name();
    test_pid();
    test_argv();
    test_env_missing();
    test_setenv_getenv();
    test_unsetenv();
    test_environ();
    test_cwd();
    test_exists_isfile_isdir();
    test_mkdir_rmdir();
    test_makedirs();
    test_remove();
    test_rename();
    test_listdir();
    test_stat();
    test_realpath();
    test_basename();
    test_dirname();
    test_splitext();
    test_join();
    test_constants();
    test_exec();
    printf("test_stdlib_os: all passed\n");
    return 0;
}
