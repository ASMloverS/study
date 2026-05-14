/* tests/unit/test_shapes.c - T20: Shape transitions and Inline Cache */
#include "../test_assert.h"
#include "ms/shape.h"
#include "ms/vm.h"
#include <stdio.h>

/* ---- Shape transition unit tests ---- */

static void test_shape_transitions(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsShape* root = ms_shape_new(&vm);
    MsObjString* x = ms_obj_string_copy(&vm, "x", 1);
    MsObjString* y = ms_obj_string_copy(&vm, "y", 1);

    MsShape* s1 = ms_shape_transition(&vm, root, x);
    MsShape* s2 = ms_shape_transition(&vm, s1, y);

    TEST_ASSERT(s1 != root);
    TEST_ASSERT(s2 != s1);
    TEST_ASSERT_EQ(ms_shape_find_slot(s2, x), 0);
    TEST_ASSERT_EQ(ms_shape_find_slot(s2, y), 1);
    TEST_ASSERT_EQ(ms_shape_find_slot(root, x), -1);
    TEST_ASSERT_EQ(ms_shape_find_slot(s1, y),   -1);

    /* Same transition must yield the same shape (sharing) */
    MsShape* s1b = ms_shape_transition(&vm, root, x);
    TEST_ASSERT(s1b == s1);

    ms_vm_free(&vm);
}

static void test_shape_root_empty(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsShape* root = ms_shape_new(&vm);
    MsObjString* z = ms_obj_string_copy(&vm, "z", 1);
    TEST_ASSERT_EQ(root->slot_count, 0u);
    TEST_ASSERT_EQ(ms_shape_find_slot(root, z), -1);

    ms_vm_free(&vm);
}

static void test_shape_multiple_properties(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsShape* s = ms_shape_new(&vm);
    MsObjString* a = ms_obj_string_copy(&vm, "a", 1);
    MsObjString* b = ms_obj_string_copy(&vm, "b", 1);
    MsObjString* c = ms_obj_string_copy(&vm, "c", 1);

    s = ms_shape_transition(&vm, s, a);
    s = ms_shape_transition(&vm, s, b);
    s = ms_shape_transition(&vm, s, c);

    TEST_ASSERT_EQ(ms_shape_find_slot(s, a), 0);
    TEST_ASSERT_EQ(ms_shape_find_slot(s, b), 1);
    TEST_ASSERT_EQ(ms_shape_find_slot(s, c), 2);
    TEST_ASSERT_EQ((int)s->slot_count, 3);

    ms_vm_free(&vm);
}

/* ---- IC integration tests (via VM interpret) ---- */

static void test_ic_field_access(void) {
    MsVM vm;
    ms_vm_init(&vm);
    /* Same property order -> same shape -> IC hit on second access */
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Point {\n"
        "  init(x, y) {\n"
        "    this.x = x\n"
        "    this.y = y\n"
        "  }\n"
        "}\n"
        "var p1 = Point(1, 2)\n"
        "var p2 = Point(3, 4)\n"
        "print(p1.x + p2.x)\n"
        "print(p1.y + p2.y)\n",
        "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_ic_polymorphic(void) {
    MsVM vm;
    ms_vm_init(&vm);
    /* Different classes sharing property name -> PIC entries */
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class A { init() { this.val = 1 } }\n"
        "class B { init() { this.val = 2 } }\n"
        "class C { init() { this.val = 3 } }\n"
        "fun get_val(obj) { return obj.val }\n"
        "print(get_val(A()))\n"
        "print(get_val(B()))\n"
        "print(get_val(C()))\n",
        "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_sbo_fields(void) {
    MsVM vm;
    ms_vm_init(&vm);
    /* Test that instances can hold more than MS_SBO_FIELDS (8) fields */
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Big {\n"
        "  init() {\n"
        "    this.a = 1\n"
        "    this.b = 2\n"
        "    this.c = 3\n"
        "    this.d = 4\n"
        "    this.e = 5\n"
        "    this.f = 6\n"
        "    this.g = 7\n"
        "    this.h = 8\n"
        "    this.i = 9\n"
        "    this.j = 10\n"
        "  }\n"
        "}\n"
        "var b = Big()\n"
        "print(b.a)\n"
        "print(b.j)\n",
        "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_ic_method_dispatch(void) {
    MsVM vm;
    ms_vm_init(&vm);
    /* Monomorphic: same class called multiple times -> IC hit on second+ call */
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Counter {\n"
        "  init() { this.n = 0 }\n"
        "  inc() { this.n = this.n + 1 }\n"
        "  val() { return this.n }\n"
        "}\n"
        "var c = Counter()\n"
        "c.inc() c.inc() c.inc()\n"
        "print(c.val())\n",
        "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_ic_method_polymorphic(void) {
    MsVM vm;
    ms_vm_init(&vm);
    /* Polymorphic: different shapes -> PIC entries; megamorphic after MS_IC_PIC_SIZE */
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class A { speak() { return 1 } }\n"
        "class B { speak() { return 2 } }\n"
        "class C { speak() { return 3 } }\n"
        "fun call_speak(obj) { return obj.speak() }\n"
        "print(call_speak(A()))\n"
        "print(call_speak(B()))\n"
        "print(call_speak(C()))\n",
        "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_shape_root_empty();
    test_shape_transitions();
    test_shape_multiple_properties();
    test_ic_field_access();
    test_ic_polymorphic();
    test_sbo_fields();
    test_ic_method_dispatch();
    test_ic_method_polymorphic();
    printf("test_shapes: all passed\n");
    return 0;
}
