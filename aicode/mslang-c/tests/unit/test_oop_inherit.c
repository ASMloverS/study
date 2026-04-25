/* tests/unit/test_oop_inherit.c - T19: inheritance, super, static, getter/setter */
#include "../test_assert.h"
#include "ms/vm.h"
#include <stdio.h>

static void test_inheritance(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class A { greet() { return \"A\" } }\n"
        "class B : A {}\n"
        "print(B().greet())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_inheritance_override(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Animal {\n"
        "  init(name) { this.name = name }\n"
        "  speak() { return this.name + \" makes a sound\" }\n"
        "}\n"
        "class Dog : Animal {\n"
        "  speak() { return this.name + \" barks\" }\n"
        "}\n"
        "class Cat : Animal {}\n"
        "var d = Dog(\"Rex\")\n"
        "print(d.speak())\n"
        "var c = Cat(\"Whiskers\")\n"
        "print(c.speak())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_super_call(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Base {\n"
        "  greet() { return \"Hello from Base\" }\n"
        "}\n"
        "class Child : Base {\n"
        "  greet() { return super.greet() + \" and Child\" }\n"
        "}\n"
        "print(Child().greet())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_super_init(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Shape {\n"
        "  init(type) { this.type = type }\n"
        "}\n"
        "class Circle : Shape {\n"
        "  init(r) {\n"
        "    super.init(\"circle\")\n"
        "    this.r = r\n"
        "  }\n"
        "}\n"
        "var c = Circle(5)\n"
        "print(c.type)\n"
        "print(c.r)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_static_methods(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class MathUtil {\n"
        "  static square(x) { return x * x }\n"
        "  static cube(x) { return x * x * x }\n"
        "}\n"
        "print(MathUtil.square(4))\n"
        "print(MathUtil.cube(3))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_getter_setter(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Temperature {\n"
        "  init(c) { this._celsius = c }\n"
        "  get celsius { return this._celsius }\n"
        "  set celsius(v) { this._celsius = v }\n"
        "  get fahrenheit { return this._celsius * 9 / 5 + 32 }\n"
        "}\n"
        "var t = Temperature(100)\n"
        "print(t.celsius)\n"
        "print(t.fahrenheit)\n"
        "t.celsius = 0\n"
        "print(t.fahrenheit)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_inheritance();
    test_inheritance_override();
    test_super_call();
    test_super_init();
    test_static_methods();
    test_getter_setter();
    printf("test_oop_inherit: all passed\n");
    return 0;
}
