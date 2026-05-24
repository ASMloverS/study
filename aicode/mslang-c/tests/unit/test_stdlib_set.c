/* tests/unit/test_stdlib_set.c - STDLIB-21: set module */
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
        fprintf(stderr, "FAIL test_stdlib_set.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_set.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- set.new / set.from_list ---- */
static void test_construction(void) {
    /* empty set */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new()\n"
        "assert(s.size() == 0)\n"
        "assert(s.is_empty() == true)\n"
    );
    /* from list */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new([1, 2, 3])\n"
        "assert(s.size() == 3)\n"
        "assert(s.contains(1) == true)\n"
        "assert(s.contains(4) == false)\n"
    );
    /* duplicates are collapsed */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new([1, 2, 2, 3, 1])\n"
        "assert(s.size() == 3)\n"
    );
    /* from_list alias */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.from_list([10, 20, 30])\n"
        "assert(s.size() == 3)\n"
        "assert(s.contains(20) == true)\n"
    );
}

/* ---- add / remove / discard / contains ---- */
static void test_mutation(void) {
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new()\n"
        "s.add(1)\n"
        "s.add(2)\n"
        "s.add(2)\n"
        "assert(s.size() == 2)\n"
        "assert(s.contains(1) == true)\n"
        "assert(s.contains(3) == false)\n"
    );
    /* remove existing */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new([1, 2, 3])\n"
        "s.remove(2)\n"
        "assert(s.size() == 2)\n"
        "assert(s.contains(2) == false)\n"
    );
    /* remove non-existent is silent */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new([1, 2])\n"
        "s.remove(99)\n"
        "assert(s.size() == 2)\n"
    );
    /* discard is alias for remove */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new([1, 2, 3])\n"
        "s.discard(2)\n"
        "assert(s.contains(2) == false)\n"
        "s.discard(99)\n"
        "assert(s.size() == 2)\n"
    );
    /* clear */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new([1, 2, 3])\n"
        "s.clear()\n"
        "assert(s.size() == 0)\n"
        "assert(s.is_empty() == true)\n"
    );
}

/* ---- to_list ---- */
static void test_to_list(void) {
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new([1, 2, 3])\n"
        "var lst = s.to_list()\n"
        "assert(len(lst) == 3)\n"
    );
    /* empty */
    RUN_OK(
        "import \"set\"\n"
        "var lst = set.new().to_list()\n"
        "assert(len(lst) == 0)\n"
    );
}

/* ---- set operations (union/intersection/difference/symmetric_difference) ---- */
static void test_set_ops(void) {
    /* union */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2, 3])\n"
        "var b = set.new([3, 4, 5])\n"
        "var u = a.union(b)\n"
        "assert(u.size() == 5)\n"
        "assert(u.contains(1) == true)\n"
        "assert(u.contains(5) == true)\n"
    );
    /* intersection */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2, 3, 4])\n"
        "var b = set.new([3, 4, 5, 6])\n"
        "var i = a.intersection(b)\n"
        "assert(i.size() == 2)\n"
        "assert(i.contains(3) == true)\n"
        "assert(i.contains(4) == true)\n"
        "assert(i.contains(1) == false)\n"
    );
    /* difference */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2, 3, 4])\n"
        "var b = set.new([3, 4, 5])\n"
        "var d = a.difference(b)\n"
        "assert(d.size() == 2)\n"
        "assert(d.contains(1) == true)\n"
        "assert(d.contains(2) == true)\n"
        "assert(d.contains(3) == false)\n"
    );
    /* symmetric_difference */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2, 3])\n"
        "var b = set.new([3, 4, 5])\n"
        "var sd = a.symmetric_difference(b)\n"
        "assert(sd.size() == 4)\n"
        "assert(sd.contains(1) == true)\n"
        "assert(sd.contains(2) == true)\n"
        "assert(sd.contains(4) == true)\n"
        "assert(sd.contains(5) == true)\n"
        "assert(sd.contains(3) == false)\n"
    );
    /* original sets are not mutated */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2, 3])\n"
        "var b = set.new([3, 4])\n"
        "var u = a.union(b)\n"
        "assert(a.size() == 3)\n"
        "assert(b.size() == 2)\n"
        "assert(u.size() == 4)\n"
    );
}

/* ---- copy ---- */
static void test_copy(void) {
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2, 3])\n"
        "var b = a.copy()\n"
        "assert(b.size() == 3)\n"
        "b.add(4)\n"
        "assert(a.size() == 3)\n"
        "assert(b.size() == 4)\n"
    );
}

/* ---- relational predicates ---- */
static void test_relations(void) {
    /* is_subset */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2])\n"
        "var b = set.new([1, 2, 3])\n"
        "assert(a.is_subset(b) == true)\n"
        "assert(b.is_subset(a) == false)\n"
        /* equal sets are subsets of each other */
        "assert(a.is_subset(set.new([1, 2])) == true)\n"
    );
    /* is_superset */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2, 3])\n"
        "var b = set.new([1, 2])\n"
        "assert(a.is_superset(b) == true)\n"
        "assert(b.is_superset(a) == false)\n"
    );
    /* is_disjoint */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2])\n"
        "var b = set.new([3, 4])\n"
        "var c = set.new([2, 3])\n"
        "assert(a.is_disjoint(b) == true)\n"
        "assert(a.is_disjoint(c) == false)\n"
    );
    /* equals */
    RUN_OK(
        "import \"set\"\n"
        "var a = set.new([1, 2, 3])\n"
        "var b = set.new([3, 1, 2])\n"
        "var c = set.new([1, 2])\n"
        "assert(a.equals(b) == true)\n"
        "assert(a.equals(c) == false)\n"
    );
    /* empty set relations */
    RUN_OK(
        "import \"set\"\n"
        "var empty = set.new()\n"
        "var nonempty = set.new([1])\n"
        "assert(empty.is_subset(nonempty) == true)\n"
        "assert(nonempty.is_superset(empty) == true)\n"
        "assert(empty.is_disjoint(nonempty) == true)\n"
        "assert(empty.equals(set.new()) == true)\n"
    );
}

/* ---- string/heterogeneous keys ---- */
static void test_value_types(void) {
    /* string elements */
    RUN_OK(
        "import \"set\"\n"
        "var s = set.new([\"a\", \"b\", \"c\"])\n"
        "assert(s.contains(\"b\") == true)\n"
        "assert(s.contains(\"x\") == false)\n"
        "s.remove(\"a\")\n"
        "assert(s.size() == 2)\n"
    );
}

/* ---- error cases ---- */
static void test_errors(void) {
    /* set.new with non-list arg */
    RUN_ERR("import \"set\"\nset.new(42)\n");
    /* set.from_list with non-list */
    RUN_ERR("import \"set\"\nset.from_list(\"bad\")\n");
}

int main(void) {
    test_construction();
    test_mutation();
    test_to_list();
    test_set_ops();
    test_copy();
    test_relations();
    test_value_types();
    test_errors();
    printf("test_stdlib_set: all passed\n");
    return 0;
}
