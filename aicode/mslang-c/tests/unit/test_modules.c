#include "test_assert.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/serializer.h"
#include "ms/fs_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <direct.h>
#else
#  include <sys/stat.h>
#endif

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

static void test_import_writes_cache(void) {
    /* Write two source files: main imports helper */
#ifdef _WIN32
    _mkdir("_t06_pkg");
#else
    mkdir("_t06_pkg", 0755);
#endif

    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, "_t06_pkg/helper.ms", "wb");
#else
    f = fopen("_t06_pkg/helper.ms", "wb");
#endif
    TEST_ASSERT(f != NULL);
    fputs("fun greet() { return \"hi\" }", f);
    fclose(f);

#ifdef _MSC_VER
    fopen_s(&f, "_t06_main.ms", "wb");
#else
    f = fopen("_t06_main.ms", "wb");
#endif
    TEST_ASSERT(f != NULL);
    fputs("import \"_t06_pkg/helper\"", f);
    fclose(f);

    /* Compile+run via ms_compile_cached; the import triggers module caching */
    MsVM vm2; ms_vm_init(&vm2);
    MsObjFunction* fn = ms_compile_cached(&vm2, "_t06_main.ms", 0);
    TEST_ASSERT(fn != NULL);

    MsObjClosure* cl = ms_obj_closure_new(&vm2, fn);
    vm2.ctx->frames[0].closure = cl;
    vm2.ctx->frames[0].ip      = fn->chunk.code;
    vm2.ctx->frames[0].slots   = vm2.ctx->stack;
    vm2.ctx->frame_count       = 1;
    int need = fn->max_stack_size + 1;
    if (need < 1) need = 1;
    vm2.ctx->stack_top = vm2.ctx->stack + need;
    TEST_ASSERT(ms_vm_run(&vm2) == MS_INTERPRET_OK);
    ms_vm_free(&vm2);

    /* Both scripts should have cache files */
    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("__mscache__/_t06_main.msc", &m));
    TEST_ASSERT(ms_fs_stat("_t06_pkg/__mscache__/helper.msc", &m));

    /* Cleanup */
    remove("__mscache__/_t06_main.msc");
    remove("_t06_pkg/__mscache__/helper.msc");
    remove("_t06_pkg/helper.ms");
#ifdef _WIN32
    _rmdir("__mscache__");
    _rmdir("_t06_pkg/__mscache__");
    _rmdir("_t06_pkg");
#else
    rmdir("__mscache__");
    rmdir("_t06_pkg/__mscache__");
    rmdir("_t06_pkg");
#endif
    remove("_t06_main.ms");
}

int main(void) {
    test_read_file();
    test_resolve_path_no_ext();
    test_resolve_path_with_ext();
    test_module_load_and_import();
    test_import_as_namespace();
    test_import_writes_cache();
    printf("test_modules: all passed\n");
    return 0;
}
