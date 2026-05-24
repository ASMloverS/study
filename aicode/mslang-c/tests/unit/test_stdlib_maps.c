/* tests/unit/test_stdlib_maps.c - STDLIB-19: maps module */
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
        fprintf(stderr, "FAIL test_stdlib_maps.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_maps.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- keys / values / items / count ---- */
static void test_extraction(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"a\": 1, \"b\": 2}\n"
        "var k = maps.keys(m)\n"
        "assert(len(k) == 2)\n"
        "var v = maps.values(m)\n"
        "assert(len(v) == 2)\n"
        "var it = maps.items(m)\n"
        "assert(len(it) == 2)\n"
        "assert(len(it[0]) == 2)\n"
        "assert(maps.count(m) == 2)\n"
    );
    /* type errors */
    RUN_ERR("import \"maps\"\nmaps.keys(42)\n");
    RUN_ERR("import \"maps\"\nmaps.count(\"x\")\n");
}

/* ---- clone / merge ---- */
static void test_clone_merge(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"a\": 1, \"b\": 2}\n"
        "var c = maps.clone(m)\n"
        "assert(maps.count(c) == 2)\n"
        "assert(c[\"a\"] == 1)\n"
    );
    RUN_OK(
        "import \"maps\"\n"
        "var m1 = {\"a\": 1, \"b\": 2}\n"
        "var m2 = {\"b\": 99, \"c\": 3}\n"
        "var merged = maps.merge(m1, m2)\n"
        "assert(merged[\"a\"] == 1)\n"
        "assert(merged[\"b\"] == 99)\n"
        "assert(merged[\"c\"] == 3)\n"
        "assert(maps.count(merged) == 3)\n"
    );
    /* merge single */
    RUN_OK(
        "import \"maps\"\n"
        "var r = maps.merge({\"x\": 10})\n"
        "assert(r[\"x\"] == 10)\n"
    );
    RUN_ERR("import \"maps\"\nmaps.merge({\"a\":1}, 42)\n");
}

/* ---- update ---- */
static void test_update(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"n\": 5}\n"
        "maps.update(m, \"n\", fun(v){ return v * 2 })\n"
        "assert(m[\"n\"] == 10)\n"
    );
    /* key absent: fn gets nil */
    RUN_OK(
        "import \"maps\"\n"
        "var m = {}\n"
        "maps.update(m, \"k\", fun(v){ return 42 })\n"
        "assert(m[\"k\"] == 42)\n"
    );
}

/* ---- get / has / pop ---- */
static void test_query(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"x\": 10}\n"
        "assert(maps.get(m, \"x\") == 10)\n"
        "assert(maps.get(m, \"y\") == nil)\n"
        "assert(maps.get(m, \"y\", 99) == 99)\n"
        "assert(maps.has(m, \"x\") == true)\n"
        "assert(maps.has(m, \"z\") == false)\n"
    );
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"a\": 1, \"b\": 2}\n"
        "assert(maps.pop(m, \"a\") == 1)\n"
        "assert(maps.has(m, \"a\") == false)\n"
        "assert(maps.count(m) == 1)\n"
        "assert(maps.pop(m, \"missing\", 0) == 0)\n"
    );
}

/* ---- filter ---- */
static void test_filter(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"a\": 1, \"b\": 2, \"c\": 3}\n"
        "var r = maps.filter(m, fun(k,v){ return v > 1 })\n"
        "assert(maps.count(r) == 2)\n"
        "assert(maps.has(r, \"a\") == false)\n"
        "assert(r[\"b\"] == 2)\n"
        "assert(r[\"c\"] == 3)\n"
    );
    RUN_ERR("import \"maps\"\nmaps.filter(42, fun(k,v){ return true })\n");
}

/* ---- map_values ---- */
static void test_map_values(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"a\": 1, \"b\": 2, \"c\": 3}\n"
        "var r = maps.map_values(m, fun(k,v){ return v * 10 })\n"
        "assert(r[\"a\"] == 10)\n"
        "assert(r[\"b\"] == 20)\n"
        "assert(r[\"c\"] == 30)\n"
    );
}

/* ---- map_keys ---- */
static void test_map_keys(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"a\": 1, \"b\": 2}\n"
        "var r = maps.map_keys(m, fun(k){ return k + \"_new\" })\n"
        "assert(maps.has(r, \"a_new\") == true)\n"
        "assert(maps.has(r, \"b_new\") == true)\n"
        "assert(maps.has(r, \"a\") == false)\n"
    );
}

/* ---- invert ---- */
static void test_invert(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"a\": 1, \"b\": 2, \"c\": 3}\n"
        "var r = maps.invert(m)\n"
        "assert(r[1] == \"a\")\n"
        "assert(r[2] == \"b\")\n"
        "assert(r[3] == \"c\")\n"
    );
}

/* ---- any / all ---- */
static void test_any_all(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var m = {\"a\": 1, \"b\": 2, \"c\": 3}\n"
        "assert(maps.any(m, fun(k,v){ return v > 2 }) == true)\n"
        "assert(maps.any(m, fun(k,v){ return v > 9 }) == false)\n"
        "assert(maps.all(m, fun(k,v){ return v > 0 }) == true)\n"
        "assert(maps.all(m, fun(k,v){ return v > 1 }) == false)\n"
    );
    /* empty map: any=false, all=true */
    RUN_OK(
        "import \"maps\"\n"
        "assert(maps.any({}, fun(k,v){ return true }) == false)\n"
        "assert(maps.all({}, fun(k,v){ return false }) == true)\n"
    );
}

/* ---- from_pairs ---- */
static void test_from_pairs(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var r = maps.from_pairs([[\"a\",1],[\"b\",2]])\n"
        "assert(r[\"a\"] == 1)\n"
        "assert(r[\"b\"] == 2)\n"
        "assert(maps.count(r) == 2)\n"
    );
    /* empty list */
    RUN_OK(
        "import \"maps\"\n"
        "var r = maps.from_pairs([])\n"
        "assert(maps.count(r) == 0)\n"
    );
    /* bad pair */
    RUN_ERR("import \"maps\"\nmaps.from_pairs([[\"a\"]])\n");
    RUN_ERR("import \"maps\"\nmaps.from_pairs(42)\n");
}

/* ---- from_keys ---- */
static void test_from_keys(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var r = maps.from_keys([\"x\",\"y\",\"z\"])\n"
        "assert(r[\"x\"] == nil)\n"
        "assert(r[\"y\"] == nil)\n"
        "var r2 = maps.from_keys([\"a\",\"b\"], 0)\n"
        "assert(r2[\"a\"] == 0)\n"
        "assert(r2[\"b\"] == 0)\n"
    );
}

/* ---- group_by ---- */
static void test_group_by(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var words = [\"apple\",\"banana\",\"avocado\",\"blueberry\"]\n"
        "var g = maps.group_by(words, fun(w){ return w[0] })\n"
        "assert(len(g[\"a\"]) == 2)\n"
        "assert(len(g[\"b\"]) == 2)\n"
    );
    RUN_OK(
        "import \"maps\"\n"
        "var g = maps.group_by([1,2,3,4], fun(x){ return x % 2 })\n"
        "assert(len(g[0]) == 2)\n"
        "assert(len(g[1]) == 2)\n"
    );
}

/* ---- zip ---- */
static void test_zip(void) {
    RUN_OK(
        "import \"maps\"\n"
        "var r = maps.zip([\"a\",\"b\",\"c\"], [1,2,3])\n"
        "assert(r[\"a\"] == 1)\n"
        "assert(r[\"b\"] == 2)\n"
        "assert(r[\"c\"] == 3)\n"
        "assert(maps.count(r) == 3)\n"
    );
    /* truncate to shortest */
    RUN_OK(
        "import \"maps\"\n"
        "var r = maps.zip([\"a\",\"b\",\"c\"], [1,2])\n"
        "assert(maps.count(r) == 2)\n"
        "assert(maps.has(r, \"c\") == false)\n"
    );
    RUN_ERR("import \"maps\"\nmaps.zip(42, [1,2])\n");
}

int main(void) {
    test_extraction();
    test_clone_merge();
    test_update();
    test_query();
    test_filter();
    test_map_values();
    test_map_keys();
    test_invert();
    test_any_all();
    test_from_pairs();
    test_from_keys();
    test_group_by();
    test_zip();
    printf("test_stdlib_maps: all passed\n");
    return 0;
}
