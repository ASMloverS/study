# Task 18: OOP — Classes, Instances, Methods

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Class declarations, instance creation, field access, method definitions, method invocation.
**Deps:** T15, T16
**Produces:** Class declarations, instantiation, field read/write, method calls, `this` binding

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `include/ms/object.h` | ObjClass, ObjInstance, ObjBoundMethod |
| Modify | `src/object.c` | Class/instance/bound-method create/destroy/print |
| Modify | `src/compiler.c` | class declaration, this, method |
| Modify | `src/vm.c` | CLASS, METHOD, GETPROP, SETPROP, INVOKE, CALL for class |
| Modify | `src/vm_call.c` | Instantiation (call `init`), bound method calls |
| Create | `tests/unit/test_oop.c` | OOP tests |

## Key Data Structures / API

```c
// Append to include/ms/object.h

typedef struct {
    MsObject obj;
    MsObjString* name;
    MsTable methods;
    MsTable* static_methods;   // lazily allocated
    MsTable* getters;          // lazily allocated
    MsTable* setters;          // lazily allocated
    MsTable* abstract_methods; // lazily allocated
    struct MsObjClass* superclass;
} MsObjClass;

// ObjInstance field storage (simplified; Shape added in T20)
typedef struct {
    MsObject obj;
    MsObjClass* klass;
    MsTable fields;
} MsObjInstance;

typedef struct {
    MsObject obj;
    MsValue receiver;       // typically ObjInstance
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

## Impl Notes

### Compiling Class Declarations

```
class Foo {
  init(x) { this.x = x }
  greet() { return "hi" }
}
```

1. `CLASS A Bx` — create `ObjClass`, name = `K(Bx)`, store in `R(A)`
2. Compile each method → `CLOSURE` → `METHOD A B` (class in `R(A)`, method name = `K(B)`, closure in `R(A-1)`)
3. `DEFGLOBAL` binds the class name

### this

`this` → local slot 0 (implicit param) in method bodies. Compiler tracks `klass` context.

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

### INVOKE (Optimized Method Call)

Direct `obj.method(args)` — no `BoundMethod` alloc:
1. Look up method name in `instance.fields` — if found (field is closure) → direct `CALL`
2. Look up in `klass.methods` → direct `CALL` with receiver in `slots[0]`

### Instantiation

Class called as fn: create `ObjInstance` → look up `init` → call `init(args)` → return instance.

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
