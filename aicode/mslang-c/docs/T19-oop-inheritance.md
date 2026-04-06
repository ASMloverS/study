# Task 19: OOP — Inheritance, Super, Static Methods

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement single inheritance, `super` calls, static methods, getters, setters, abstract methods.
**Dependencies:** T18
**Produces:** 完整 OOP: 继承链, super 调用, 静态方法, getter/setter, 抽象方法

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/compiler.c` | super 语法, static, getter/setter/abstract |
| Modify | `src/vm.c` | INHERIT, GETSUPER, SUPERINV, STATICMETH, GETTER, SETTER, ABSTMETH |
| Modify | `src/vm_call.c` | super 方法调用, 静态方法调用 |
| Create | `tests/unit/test_oop_inherit.c` | 继承测试 |

## Implementation Notes

### INHERIT

`class Sub : Super` → 编译器发射 `INHERIT A B` (A=子类 reg, B=父类 reg).
VM 中: `sub->superclass = super; ms_table_add_all(&sub->methods, &super->methods)` (方法继承).

### super

`super.method(args)` 编译为:
1. 在编译器中: super 解析为 upvalue (指向外层 scope 的特殊 local)
2. `GETSUPER A B C` — A=结果, B=receiver(this), C=方法名常量
3. VM 中: 从 superclass 的方法表查找, 创建 BoundMethod

`SUPERINV A B C` — 优化的 super 方法调用 (无需创建 BoundMethod):
1. 从 super 的方法表查方法
2. 直接以 this 为 receiver 调用

### Static Methods

`static greet() { ... }` → `STATICMETH A B` (A=class, B=method name).
VM 中: `ms_table_set(class->static_methods, name, closure)`.
访问: `ClassName.staticMethod()` → GETPROP 先查 instance fields, 再查 methods, 最后查 static_methods.

### Getter / Setter

```ms
class Circle {
  init(r) { this.r = r }
  get area { return 3.14159 * this.r * this.r }
  set radius(v) { this.r = v }
}
```
- `GETTER A B`: 将闭包注册为 getter
- `SETTER A B`: 将闭包注册为 setter
- GETPROP 时: 若 name 在 getters 表中, 直接调用 getter (0 参数) 并返回结果
- SETPROP 时: 若 name 在 setters 表中, 调用 setter (1 参数)

### Abstract Methods

`abstract foo()` → `ABSTMETH A B`. 在 methods 表中放一个标记值.
实例化时: 若类有未实现的 abstract methods → runtime error.

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
