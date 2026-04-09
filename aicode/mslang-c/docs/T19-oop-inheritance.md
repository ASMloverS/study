# Task 19: OOP — Inheritance, Super, Static Methods

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement single inheritance, `super` calls, static methods, getters, setters, abstract methods.
**Dependencies:** T18
**Produces:** Complete OOP: inheritance chain, super calls, static methods, getter/setter, abstract methods

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/compiler.c` | super syntax, static, getter/setter/abstract |
| Modify | `src/vm.c` | INHERIT, GETSUPER, SUPERINV, STATICMETH, GETTER, SETTER, ABSTMETH |
| Modify | `src/vm_call.c` | super method calls, static method calls |
| Create | `tests/unit/test_oop_inherit.c` | Inheritance tests |

## Implementation Notes

### INHERIT

`class Sub : Super` → compiler emits `INHERIT A B` (A=subclass reg, B=superclass reg).
VM: `sub->superclass = super; ms_table_add_all(&sub->methods, &super->methods)` (method inheritance).

### super

`super.method(args)` compiles to:
1. In the compiler: `super` resolves as an upvalue (a special local in an outer scope)
2. `GETSUPER A B C` — A=result, B=receiver (`this`), C=method name constant
3. VM: look up in superclass method table, create `BoundMethod`

`SUPERINV A B C` — optimized super method call (no `BoundMethod` created):
1. Look up method in super's method table
2. Call directly with `this` as receiver

### Static Methods

`static greet() { ... }` → `STATICMETH A B` (A=class, B=method name).
VM: `ms_table_set(class->static_methods, name, closure)`.
Access: `ClassName.staticMethod()` → `GETPROP` checks `instance.fields`, then `methods`, then `static_methods`.

### Getter / Setter

```ms
class Circle {
  init(r) { this.r = r }
  get area { return 3.14159 * this.r * this.r }
  set radius(v) { this.r = v }
}
```
- `GETTER A B`: registers closure as getter
- `SETTER A B`: registers closure as setter
- On `GETPROP`: if name is in getters table, call getter (0 args) and return result
- On `SETPROP`: if name is in setters table, call setter (1 arg)

### Abstract Methods

`abstract foo()` → `ABSTMETH A B`. Places a sentinel value in the methods table.
On instantiation: if the class has unimplemented abstract methods → runtime error.

## C Unit Tests

```c
// tests/unit/test_oop_inherit.c
#include "test_assert.h"
#include "ms/vm.h"

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

int main(void) {
    test_inheritance();
    printf("test_oop_inherit: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/inheritance.ms
class Animal {
  init(name) { this.name = name }
  speak() { return this.name + " makes a sound" }
}
class Dog : Animal {
  speak() { return this.name + " barks" }
}
class Cat : Animal {}

var d = Dog("Rex")
print(d.speak())
// expect: Rex barks
var c = Cat("Whiskers")
print(c.speak())
// expect: Whiskers makes a sound

// tests/fixtures/super.ms
class Base {
  greet() { return "Hello from Base" }
}
class Child : Base {
  greet() { return super.greet() + " and Child" }
}
print(Child().greet())
// expect: Hello from Base and Child

// tests/fixtures/super_init.ms
class Shape {
  init(type) { this.type = type }
}
class Circle : Shape {
  init(r) {
    super.init("circle")
    this.r = r
  }
}
var c = Circle(5)
print(c.type)
// expect: circle
print(c.r)
// expect: 5

// tests/fixtures/static_methods.ms
class MathUtil {
  static square(x) { return x * x }
  static cube(x) { return x * x * x }
}
print(MathUtil.square(4))
// expect: 16
print(MathUtil.cube(3))
// expect: 27

// tests/fixtures/getter_setter.ms
class Temperature {
  init(c) { this._celsius = c }
  get celsius { return this._celsius }
  set celsius(v) { this._celsius = v }
  get fahrenheit { return this._celsius * 9 / 5 + 32 }
}
var t = Temperature(100)
print(t.celsius)
// expect: 100
print(t.fahrenheit)
// expect: 212
t.celsius = 0
print(t.fahrenheit)
// expect: 32
```
