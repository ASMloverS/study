# Task 18: OOP — Classes, Instances, Methods

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement class declarations, instance creation, field access, method definitions, and method invocation.
**Dependencies:** T15, T16
**Produces:** 类声明, 实例化, 字段读写, 方法调用, this 绑定

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `include/ms/object.h` | ObjClass, ObjInstance, ObjBoundMethod |
| Modify | `src/object.c` | 类/实例/绑定方法 创建/销毁/打印 |
| Modify | `src/compiler.c` | class 声明, this, method |
| Modify | `src/vm.c` | CLASS, METHOD, GETPROP, SETPROP, INVOKE, CALL for class |
| Modify | `src/vm_call.c` | 实例化 (调用 init), 绑定方法调用 |
| Create | `tests/unit/test_oop.c` | OOP 测试 |

## Key Data Structures / API

```c
// 追加到 include/ms/object.h

typedef struct {
    MsObject obj;
    MsObjString* name;
    MsTable methods;
    MsTable* static_methods;  // 懒分配
    MsTable* getters;         // 懒分配
    MsTable* setters;         // 懒分配
    MsTable* abstract_methods; // 懒分配
    struct MsObjClass* superclass;
} MsObjClass;

// ObjInstance 字段存储 (简化版, Shape 在 T20)
typedef struct {
    MsObject obj;
    MsObjClass* klass;
    MsTable fields;
} MsObjInstance;

typedef struct {
    MsObject obj;
    MsValue receiver;       // 通常是 ObjInstance
    MsObjClosure* method;
} MsObjBoundMethod;

#define MS_IS_CLASS(v)         MS_IS_OBJ_TYPE(v, MS_OBJ_CLASS)
#define MS_AS_CLASS(v)         ((MsObjClass*)MS_AS_OBJECT(v))
#define MS_IS_INSTANCE(v)      MS_IS_OBJ_TYPE(v, MS_OBJ_INSTANCE)
#define MS_AS_INSTANCE(v)      ((MsObjInstance*)MS_AS_OBJECT(v))
#define MS_IS_BOUND_METHOD(v)  MS_IS_OBJ_TYPE(v, MS_OBJ_BOUND_METHOD)
#define MS_AS_BOUND_METHOD(v)  ((MsObjBoundMethod*)MS_AS_OBJECT(v))

MsObjClass*       ms_obj_class_new(MsVM* vm, MsObjString* name);
MsObjInstance*    ms_obj_instance_new(MsVM* vm, MsObjClass* klass);
MsObjBoundMethod* ms_obj_bound_method_new(MsVM* vm, MsValue receiver,
                                           MsObjClosure* method);
```

## Implementation Notes

### 编译 class 声明

```
class Foo {
  init(x) { this.x = x }
  greet() { return "hi" }
}
```

1. `CLASS A Bx` — 创建 ObjClass, 名字 = K(Bx), 放入 R(A)
2. 编译每个方法 → CLOSURE → `METHOD A B` (class 在 R(A), 方法名 = K(B), 闭包在 R(A-1))
3. `DEFGLOBAL` 绑定类名

### this

`this` 在方法体中解析为 local slot 0 (隐式参数). 编译器追踪 `klass` 上下文.

### GETPROP / SETPROP

```c
case MS_OP_GETPROP: {
    MsValue obj = R(B);
    if (!MS_IS_INSTANCE(obj)) { runtime_error("..."); break; }
    MsObjInstance* inst = MS_AS_INSTANCE(obj);
    MsObjString* name = MS_AS_STRING(K(C));
    MsValue val;
    if (ms_table_get(&inst->fields, name, &val)) {
        R(A) = val;
    } else if (!bind_method(vm, inst->klass, name, A)) {
        runtime_error("Undefined property '%s'.", name->data);
    }
    break;
}
```

### INVOKE (优化的方法调用)

`obj.method(args)` 直接调用, 无需创建 BoundMethod:
1. 在 instance.fields 查方法名 — 若找到 (field is closure) → 直接 CALL
2. 在 klass.methods 查 → 直接 CALL with receiver in slots[0]

### 实例化

调用 class 作为函数: 创建 ObjInstance, 查找 `init` 方法, 调用 init(args), 返回实例.

## C Unit Tests

```c
// tests/unit/test_oop.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_class_creation(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "class Foo {}\nprint(Foo)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_class_creation();
    printf("test_oop: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/classes.ms
class Point {
  init(x, y) {
    this.x = x
    this.y = y
  }
  toString() {
    return "(" + this.x + ", " + this.y + ")"
  }
}
var p = Point(1, 2)
print(p.x)
// expect: 1
print(p.y)
// expect: 2

// tests/fixtures/methods.ms
class Counter {
  init() {
    this.count = 0
  }
  increment() {
    this.count = this.count + 1
  }
  get() {
    return this.count
  }
}
var c = Counter()
c.increment()
c.increment()
c.increment()
print(c.get())
// expect: 3

// tests/fixtures/this_binding.ms
class Greeter {
  init(name) {
    this.name = name
  }
  greet() {
    return "Hello, " + this.name + "!"
  }
}
var g = Greeter("World")
print(g.greet())
// expect: Hello, World!

// tests/fixtures/class_fields.ms
class Box {
  init() {
    this.value = nil
  }
  set(v) { this.value = v }
  get() { return this.value }
}
var b = Box()
b.set(42)
print(b.get())
// expect: 42
b.set("hello")
print(b.get())
// expect: hello
```
