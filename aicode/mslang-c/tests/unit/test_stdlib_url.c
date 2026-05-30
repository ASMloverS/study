/* tests/unit/test_stdlib_url.c - STDLIB-40: url module */
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
        fprintf(stderr, "FAIL test_stdlib_url.c:%d: expected OK\n", line);
        exit(1);
    }
}

#define RUN_OK(src) check_ok((src), __LINE__)

/* ---- encode / decode ---- */
static void test_encode_decode(void) {
    RUN_OK(
        "import \"url\"\n"
        "assert(url.encode(\"hello world!\") == \"hello%20world%21\")\n"
        "assert(url.encode(\"abc123-_.~\") == \"abc123-_.~\")\n"
        "assert(url.encode(\"\") == \"\")\n"
    );
    RUN_OK(
        "import \"url\"\n"
        "assert(url.decode(\"hello%20world\") == \"hello world\")\n"
        "assert(url.decode(\"abc%21%40%23\") == \"abc!@#\")\n"
        "assert(url.decode(\"\") == \"\")\n"
        "assert(url.decode(\"no-escapes\") == \"no-escapes\")\n"
    );
}

/* ---- encode_path ---- */
static void test_encode_path(void) {
    RUN_OK(
        "import \"url\"\n"
        "assert(url.encode_path(\"/foo bar/baz\") == \"/foo%20bar/baz\")\n"
        "assert(url.encode_path(\"/path/no-escapes_here.txt\") == \"/path/no-escapes_here.txt\")\n"
    );
}

/* ---- encode_query ---- */
static void test_encode_query(void) {
    RUN_OK(
        "import \"url\"\n"
        "assert(url.encode_query(\"hello world\") == \"hello+world\")\n"
        "assert(url.encode_query(\"a=1\") == \"a%3D1\")\n"
    );
}

/* ---- build_query ---- */
static void test_build_query(void) {
    RUN_OK(
        "import \"url\"\n"
        "var q = url.build_query({\"a\": \"1\", \"b\": \"2\"})\n"
        "assert(q.len() > 0)\n"
    );
}

/* ---- build ---- */
static void test_build(void) {
    RUN_OK(
        "import \"url\"\n"
        "var s = url.build(\"https\", \"example.com\", \"/path\", nil, nil)\n"
        "assert(s == \"https://example.com/path\")\n"
    );
    RUN_OK(
        "import \"url\"\n"
        "var s = url.build(\"http\", \"host.com\", \"/p\", \"k=v\", \"frag\")\n"
        "assert(s == \"http://host.com/p?k=v#frag\")\n"
    );
}

/* ---- parse fields ---- */
static void test_parse_fields(void) {
    RUN_OK(
        "import \"url\"\n"
        "var u = url.parse(\"https://example.com/path\")\n"
        "assert(u.scheme == \"https\")\n"
        "assert(u.host == \"example.com\")\n"
        "assert(u.port == \"\")\n"
        "assert(u.path == \"/path\")\n"
        "assert(u.raw_query == \"\")\n"
        "assert(u.fragment == \"\")\n"
        "assert(u.userinfo == \"\")\n"
    );
    RUN_OK(
        "import \"url\"\n"
        "var u = url.parse(\"https://user:pass@example.com:8080/path?a=1&b=2#frag\")\n"
        "assert(u.scheme == \"https\")\n"
        "assert(u.host == \"example.com\")\n"
        "assert(u.port == \"8080\")\n"
        "assert(u.path == \"/path\")\n"
        "assert(u.raw_query == \"a=1&b=2\")\n"
        "assert(u.fragment == \"frag\")\n"
        "assert(u.userinfo == \"user:pass\")\n"
    );
}

/* ---- query_params ---- */
static void test_query_params(void) {
    RUN_OK(
        "import \"url\"\n"
        "var u = url.parse(\"http://host/p?a=1&b=hello\")\n"
        "var m = u.query_params()\n"
        "assert(m[\"a\"] == \"1\")\n"
        "assert(m[\"b\"] == \"hello\")\n"
    );
    RUN_OK(
        "import \"url\"\n"
        "var u = url.parse(\"http://host/p\")\n"
        "var m = u.query_params()\n"
        "assert(m.len() == 0)\n"
    );
}

/* ---- to_str ---- */
static void test_to_str(void) {
    RUN_OK(
        "import \"url\"\n"
        "var u = url.parse(\"https://example.com/path?q=1#frag\")\n"
        "var s = u.to_str()\n"
        "assert(s == \"https://example.com/path?q=1#frag\")\n"
    );
    RUN_OK(
        "import \"url\"\n"
        "var u = url.parse(\"https://user:pass@example.com:8080/path?a=1#f\")\n"
        "var s = u.to_str()\n"
        "assert(s == \"https://user:pass@example.com:8080/path?a=1#f\")\n"
    );
}

/* ---- join ---- */
static void test_join(void) {
    RUN_OK(
        "import \"url\"\n"
        "var u = url.parse(\"https://example.com/base/page\")\n"
        "assert(u.join(\"other\") == \"https://example.com/base/other\")\n"
    );
    RUN_OK(
        "import \"url\"\n"
        "var u = url.parse(\"https://example.com/base/\")\n"
        "assert(u.join(\"https://other.com/x\") == \"https://other.com/x\")\n"
    );
}

/* ---- try_parse ---- */
static void test_try_parse(void) {
    RUN_OK(
        "import \"url\"\n"
        "var u = url.try_parse(\"https://example.com/ok\")\n"
        "assert(u != nil)\n"
        "assert(u.scheme == \"https\")\n"
    );
    RUN_OK(
        "import \"url\"\n"
        "var u = url.try_parse(\"not a url\")\n"
        "assert(u == nil)\n"
    );
}

int main(void) {
    test_encode_decode();
    test_encode_path();
    test_encode_query();
    test_build_query();
    test_build();
    test_parse_fields();
    test_query_params();
    test_to_str();
    test_join();
    test_try_parse();
    printf("test_stdlib_url: all passed\n");
    return 0;
}
