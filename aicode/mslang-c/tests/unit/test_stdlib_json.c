/* tests/unit/test_stdlib_json.c - STDLIB-16: json module */
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
        fprintf(stderr, "FAIL test_stdlib_json.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_json.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- nil/bool roundtrip ---- */
static void test_nil_bool(void) {
    RUN_OK("import \"json\"\nassert(json.encode(nil) == \"null\")\n");
    RUN_OK("import \"json\"\nassert(json.encode(true) == \"true\")\n");
    RUN_OK("import \"json\"\nassert(json.encode(false) == \"false\")\n");
    RUN_OK("import \"json\"\nassert(json.decode(\"null\") == nil)\n");
    RUN_OK("import \"json\"\nassert(json.decode(\"true\") == true)\n");
    RUN_OK("import \"json\"\nassert(json.decode(\"false\") == false)\n");
}

/* ---- int roundtrip ---- */
static void test_int(void) {
    RUN_OK("import \"json\"\nassert(json.encode(42) == \"42\")\n");
    RUN_OK("import \"json\"\nassert(json.encode(-7) == \"-7\")\n");
    RUN_OK("import \"json\"\nassert(json.encode(0) == \"0\")\n");
    /* decode integer (no decimal point) -> int */
    RUN_OK("import \"json\"\nvar v = json.decode(\"1\")\nassert(v == 1)\n");
    RUN_OK("import \"json\"\nvar v = json.decode(\"-99\")\nassert(v == -99)\n");
    /* large int64 boundary */
    RUN_OK("import \"json\"\nvar v = json.decode(\"9223372036854775807\")\nassert(v == 9223372036854775807)\n");
}

/* ---- num roundtrip ---- */
static void test_num(void) {
    /* 1.0 decodes as num */
    RUN_OK("import \"json\"\nvar v = json.decode(\"1.0\")\nassert(v == 1.0)\n");
    RUN_OK("import \"json\"\nvar v = json.decode(\"3.14\")\nassert(v >= 3.13 and v <= 3.15)\n");
    /* encode float */
    RUN_OK("import \"json\"\nvar s = json.encode(1.5)\nassert(s != nil)\n");
}

/* ---- string roundtrip ---- */
static void test_string(void) {
    RUN_OK("import \"json\"\nassert(json.encode(\"hello\") == \"\\\"hello\\\"\")\n");
    RUN_OK("import \"json\"\nassert(json.decode(\"\\\"hello\\\"\") == \"hello\")\n");
    /* escape sequences */
    RUN_OK("import \"json\"\nassert(json.decode(\"\\\"a\\\\nb\\\"\") == \"a\\nb\")\n");
    /* \\t */
    RUN_OK("import \"json\"\nassert(json.decode(\"\\\"\\\\t\\\"\") == \"\\t\")\n");
}

/* ---- list roundtrip ---- */
static void test_list(void) {
    RUN_OK("import \"json\"\nassert(json.encode([]) == \"[]\")\n");
    RUN_OK("import \"json\"\nassert(json.encode([1,2,3]) == \"[1,2,3]\")\n");
    RUN_OK("import \"json\"\nvar v = json.decode(\"[1,2,3]\")\nassert(v[0] == 1 and v[1] == 2 and v[2] == 3)\n");
    RUN_OK("import \"json\"\nvar v = json.decode(\"[]\")\nassert(v != nil)\n");
}

/* ---- map roundtrip ---- */
static void test_map(void) {
    RUN_OK("import \"json\"\nassert(json.encode({}) == \"{}\")\n");
    /* encode/decode with string key */
    RUN_OK("import \"json\"\nvar s = json.encode({\"k\": 1})\nvar v = json.decode(s)\nassert(v[\"k\"] == 1)\n");
    /* nested */
    RUN_OK(
        "import \"json\"\n"
        "var obj = {\"a\": [1, 2], \"b\": {\"c\": true}}\n"
        "var s = json.encode(obj)\n"
        "var v = json.decode(s)\n"
        "assert(v[\"a\"][0] == 1)\n"
        "assert(v[\"b\"][\"c\"] == true)\n"
    );
}

/* ---- duplicate key: last wins ---- */
static void test_dup_key(void) {
    RUN_OK("import \"json\"\nvar v = json.decode(\"{\\\"k\\\":1,\\\"k\\\":2}\")\nassert(v[\"k\"] == 2)\n");
}

/* ---- is_valid ---- */
static void test_is_valid(void) {
    RUN_OK("import \"json\"\nassert(json.is_valid(\"null\") == true)\n");
    RUN_OK("import \"json\"\nassert(json.is_valid(\"42\") == true)\n");
    RUN_OK("import \"json\"\nassert(json.is_valid(\"bad json\") == false)\n");
    RUN_OK("import \"json\"\nassert(json.is_valid(\"\") == false)\n");
}

/* ---- try_decode ---- */
static void test_try_decode(void) {
    RUN_OK("import \"json\"\nassert(json.try_decode(\"null\") == nil)\n");
    RUN_OK("import \"json\"\nassert(json.try_decode(\"42\") == 42)\n");
    RUN_OK("import \"json\"\nassert(json.try_decode(\"bad\") == nil)\n");
}

/* ---- encode_pretty smoke ---- */
static void test_encode_pretty(void) {
    RUN_OK("import \"json\"\nvar s = json.encode_pretty({\"a\": 1}, 2)\nassert(s != nil)\n");
    RUN_OK("import \"json\"\nvar s = json.encode_pretty([1,2], 4)\nassert(s != nil)\n");
    /* default indent */
    RUN_OK("import \"json\"\nvar s = json.encode_pretty({\"x\": 1})\nassert(s != nil)\n");
}

/* ---- error: NaN/Infinity ---- */
static void test_nan_inf_error(void) {
    RUN_ERR("import \"json\"\njson.encode(0.0/0.0)\n");
    RUN_ERR("import \"json\"\njson.encode(1.0/0.0)\n");
}

/* ---- error: non-string map key ---- */
static void test_non_string_key(void) {
    /* map with int key should error */
    RUN_ERR("import \"json\"\nvar m = {}\nm[1] = 2\njson.encode(m)\n");
}

/* ---- error: unserializable type ---- */
static void test_unserializable(void) {
    RUN_ERR("import \"json\"\nvar f = fn() nil end\njson.encode(f)\n");
}

/* ---- error: bad JSON ---- */
static void test_decode_error(void) {
    RUN_ERR("import \"json\"\njson.decode(\"bad\")\n");
    RUN_ERR("import \"json\"\njson.decode(\"[1,2,\")\n");
    RUN_ERR("import \"json\"\njson.decode(\"\")\n");
}

int main(void) {
    test_nil_bool();
    test_int();
    test_num();
    test_string();
    test_list();
    test_map();
    test_dup_key();
    test_is_valid();
    test_try_decode();
    test_encode_pretty();
    test_nan_inf_error();
    test_non_string_key();
    test_unserializable();
    test_decode_error();
    printf("test_stdlib_json: all passed\n");
    return 0;
}
