/* tests/unit/test_oop.c - T18: OOP classes, instances, methods */
#include "../test_assert.h"
#include "ms/vm.h"
#include <stdio.h>

static void test_class_creation(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Foo {}\nprint(Foo)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_instance_creation(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Foo {}\nvar f = Foo()\nprint(f)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_init_and_fields(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Point {\n"
        "  init(x, y) { this.x = x\nthis.y = y }\n"
        "}\n"
        "var p = Point(1, 2)\n"
        "print(p.x)\n"
        "print(p.y)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_method_call(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Counter {\n"
        "  init() { this.count = 0 }\n"
        "  increment() { this.count = this.count + 1 }\n"
        "  get() { return this.count }\n"
        "}\n"
        "var c = Counter()\n"
        "c.increment()\n"
        "c.increment()\n"
        "c.increment()\n"
        "print(c.get())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_this_binding(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Greeter {\n"
        "  init(name) { this.name = name }\n"
        "  greet() { return \"Hello, \" + this.name + \"!\" }\n"
        "}\n"
        "var g = Greeter(\"World\")\n"
        "print(g.greet())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_field_set_get(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Box {\n"
        "  init() { this.value = nil }\n"
        "  set(v) { this.value = v }\n"
        "  get() { return this.value }\n"
        "}\n"
        "var b = Box()\n"
        "b.set(42)\n"
        "print(b.get())\n"
        "b.set(\"hello\")\n"
        "print(b.get())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_class_creation();
    test_instance_creation();
    test_init_and_fields();
    test_method_call();
    test_this_binding();
    test_field_set_get();
    printf("test_oop: all passed\n");
    return 0;
}
