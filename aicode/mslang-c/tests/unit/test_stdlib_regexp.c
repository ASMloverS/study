/* tests/unit/test_stdlib_regexp.c - STDLIB-28: regexp module */
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
        fprintf(stderr, "FAIL test_stdlib_regexp.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_regexp.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- is_match ---- */
static void test_is_match(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "assert(regexp.is_match(\"[0-9]+\", \"abc123\") == true)\n"
        "assert(regexp.is_match(\"[0-9]+\", \"abc\")    == false)\n"
        "assert(regexp.is_match(\"^hello\", \"hello world\") == true)\n"
        "assert(regexp.is_match(\"^world\", \"hello world\") == false)\n"
    );
}

/* ---- match anchored at start ---- */
static void test_match(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "var m = regexp.match(\"hello\", \"hello world\")\n"
        "assert(m != nil)\n"
        "assert(m.matched == \"hello\")\n"
        "assert(m.start == 0)\n"
        "assert(m.end == 5)\n"
    );
    RUN_OK(
        "import \"regexp\"\n"
        "var m = regexp.match(\"world\", \"hello world\")\n"
        "assert(m == nil)\n"
    );
}

/* ---- search ---- */
static void test_search(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "var m = regexp.search(\"[0-9]+\", \"abc123def\")\n"
        "assert(m != nil)\n"
        "assert(m.matched == \"123\")\n"
        "assert(m.start == 3)\n"
        "assert(m.end == 6)\n"
    );
}

/* ---- capture groups ---- */
static void test_groups(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "var m = regexp.search(\"([0-9]+)-([a-z]+)\", \"order-42-abc\")\n"
        "assert(m != nil)\n"
        "assert(m.matched == \"42-abc\")\n"
        "assert(m.group(1) == \"42\")\n"
        "assert(m.group(2) == \"abc\")\n"
        "var g = m.groups()\n"
        "assert(g[0] == \"42\")\n"
        "assert(g[1] == \"abc\")\n"
    );
    RUN_OK(
        "import \"regexp\"\n"
        "var m = regexp.search(\"([0-9]+)\", \"abc\")\n"
        "assert(m == nil)\n"
    );
}

/* ---- anchors ---- */
static void test_anchors(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "assert(regexp.is_match(\"^abc\", \"abcdef\") == true)\n"
        "assert(regexp.is_match(\"^abc\", \"xabcdef\") == false)\n"
        "assert(regexp.is_match(\"def$\", \"abcdef\") == true)\n"
        "assert(regexp.is_match(\"def$\", \"abcdefx\") == false)\n"
    );
}

/* ---- character classes ---- */
static void test_char_classes(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "assert(regexp.is_match(\"[aeiou]+\", \"hello\") == true)\n"
        "assert(regexp.is_match(\"[^aeiou]+\", \"bcd\") == true)\n"
        "assert(regexp.is_match(\"[a-z]+\", \"abc\") == true)\n"
        "assert(regexp.is_match(\"[A-Z]+\", \"abc\") == false)\n"
    );
}

/* ---- quantifiers ---- */
static void test_quantifiers(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "assert(regexp.is_match(\"a*\",  \"aaa\") == true)\n"
        "assert(regexp.is_match(\"a+\",  \"aaa\") == true)\n"
        "assert(regexp.is_match(\"a+\",  \"bbb\") == false)\n"
        "assert(regexp.is_match(\"ab?\", \"a\")   == true)\n"
        "assert(regexp.is_match(\"ab?\", \"ab\")  == true)\n"
    );
}

/* ---- case-insensitive (?i) ---- */
static void test_flag_i(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "assert(regexp.is_match(\"(?i)hello\", \"HELLO\") == true)\n"
        "assert(regexp.is_match(\"(?i)hello\", \"HeLLo\") == true)\n"
        "assert(regexp.is_match(\"hello\",     \"HELLO\") == false)\n"
    );
}

/* ---- find_all ---- */
static void test_find_all(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "var all = regexp.find_all(\"[0-9]+\", \"a1b22c333\")\n"
        "assert(all.len() == 3)\n"
        "assert(all[0].matched == \"1\")\n"
        "assert(all[1].matched == \"22\")\n"
        "assert(all[2].matched == \"333\")\n"
    );
    /* zero-width guard: find_all on zero-width matches should terminate */
    RUN_OK(
        "import \"regexp\"\n"
        "var all = regexp.find_all(\"a*\", \"bb\")\n"
        "assert(all.len() >= 2)\n"
    );
}

/* ---- replace (string repl) ---- */
static void test_replace_str(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "var r = regexp.replace(\"[0-9]+\", \"a1b2c3\", \"X\")\n"
        "assert(r == \"aXbXcX\")\n"
    );
    RUN_OK(
        "import \"regexp\"\n"
        "var r = regexp.replace(\"[ ]+\", \"hello   world\", \" \")\n"
        "assert(r == \"hello world\")\n"
    );
}

/* ---- replace_n ---- */
static void test_replace_n(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "var r = regexp.replace_n(\"[0-9]+\", \"a1b2c3\", \"X\", 2)\n"
        "assert(r == \"aXbXc3\")\n"
    );
}

/* ---- split ---- */
static void test_split(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "var parts = regexp.split(\"[ ]+\", \"  hello   world  \")\n"
        "assert(parts.len() >= 2)\n"
        "var found = false\n"
        "for (var i = 0; i < parts.len(); i = i + 1) {\n"
        "    if (parts[i] == \"hello\") { found = true }\n"
        "}\n"
        "assert(found == true)\n"
    );
}

/* ---- compile + Regex instance methods ---- */
static void test_compile(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "var re = regexp.compile(\"[0-9]+\")\n"
        "assert(re != nil)\n"
        "var m = re.search(\"abc123\")\n"
        "assert(m != nil)\n"
        "assert(m.matched == \"123\")\n"
    );
    RUN_OK(
        "import \"regexp\"\n"
        "var re = regexp.compile(\"([a-z]+)@([a-z]+)\")\n"
        "var m = re.match(\"user@host\")\n"
        "assert(m != nil)\n"
        "assert(m.group(1) == \"user\")\n"
        "assert(m.group(2) == \"host\")\n"
    );
    RUN_OK(
        "import \"regexp\"\n"
        "var re = regexp.compile(\"[0-9]+\")\n"
        "var all = re.find_all(\"1 22 333\")\n"
        "assert(all.len() == 3)\n"
    );
}

/* ---- alternation ---- */
static void test_alternation(void) {
    RUN_OK(
        "import \"regexp\"\n"
        "assert(regexp.is_match(\"cat|dog\", \"I have a dog\") == true)\n"
        "assert(regexp.is_match(\"cat|dog\", \"I have a fish\") == false)\n"
    );
}

/* ---- invalid pattern error ---- */
static void test_invalid_pattern(void) {
    RUN_ERR(
        "import \"regexp\"\n"
        "regexp.search(\"[\", \"abc\")\n"
    );
}

int main(void) {
    test_is_match();
    test_match();
    test_search();
    test_groups();
    test_anchors();
    test_char_classes();
    test_quantifiers();
    test_flag_i();
    test_find_all();
    test_replace_str();
    test_replace_n();
    test_split();
    test_compile();
    test_alternation();
    test_invalid_pattern();
    printf("test_stdlib_regexp: all tests passed\n");
    return 0;
}
