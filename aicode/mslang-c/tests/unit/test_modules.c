#include "test_assert.h"
#include "ms/module.h"
#include "ms/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stdout capture helpers ---- */

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  define DUP(fd)         _dup(fd)
#  define DUP2(src, dst)  _dup2((src), (dst))
#  define FILENO(f)       _fileno(f)
#  define TMPPATH "test_modules_out.tmp"
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

static void test_read_file(void) {
    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, "_test_mod.ms", "w");
#else
    f = fopen("_test_mod.ms", "w");
#endif
    fprintf(f, "var x = 42\n");
    fclose(f);
    char* src = ms_read_file("_test_mod.ms");
    TEST_ASSERT(src != NULL);
    TEST_ASSERT(strstr(src, "42") != NULL);
    free(src);
    remove("_test_mod.ms");
}

static void test_resolve_path_no_ext(void) {
    char* p = ms_resolve_path("math", "/some/dir");
    TEST_ASSERT(p != NULL);
    TEST_ASSERT(strstr(p, ".ms") != NULL);
    free(p);
}

static void test_resolve_path_with_ext(void) {
    char* p = ms_resolve_path("math.ms", "/dir");
    TEST_ASSERT(p != NULL);
    /* Should NOT double-append .ms */
    const char* first = strstr(p, ".ms");
    TEST_ASSERT(first != NULL);
    const char* second = strstr(first + 1, ".ms");
    TEST_ASSERT(second == NULL);
    free(p);
}

static void test_module_load_and_import(void) {
    /* Write a small module to disk */
    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, "_mod_math.ms", "w");
#else
    f = fopen("_mod_math.ms", "w");
#endif
    fprintf(f, "var PI = 3\nfun double(x) { return x + x }\n");
    fclose(f);

    /* Run a script that imports it */
    char buf[256];
    MsInterpretResult r = run_capture(
        "from \"_mod_math\" import PI\nprint(PI)\n",
        buf, sizeof(buf));
    TEST_ASSERT(r == MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "3") != NULL);
    remove("_mod_math.ms");
}

static void test_import_as_namespace(void) {
    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, "_mod_ns.ms", "w");
#else
    f = fopen("_mod_ns.ms", "w");
#endif
    fprintf(f, "var value = 99\n");
    fclose(f);

    char buf[256];
    MsInterpretResult r = run_capture(
        "import \"_mod_ns\"\nprint(_mod_ns.value)\n",
        buf, sizeof(buf));
    TEST_ASSERT(r == MS_INTERPRET_OK);
    TEST_ASSERT(strstr(buf, "99") != NULL);
    remove("_mod_ns.ms");
}

int main(void) {
    test_read_file();
    test_resolve_path_no_ext();
    test_resolve_path_with_ext();
    test_module_load_and_import();
    test_import_as_namespace();
    printf("test_modules: all passed\n");
    return 0;
}
