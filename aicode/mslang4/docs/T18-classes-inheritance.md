# T18: Classes & Inheritance

**Phase**: 9 · **Deps**: T16 (Functions & Closures), T17 (Garbage Collection) · **Complexity**: High

## Goal

Class declarations, instance creation, field access, methods, constructors (`init`), single inheritance, `this`, `super`. Extends compiler, object system, VM.

## Files

| File | Changes |
|------|---------|
| `src/object.h` | `MsClass`, `MsInstance`, `MsBoundMethod` |
| `src/object.c` | Class/instance/bound method create + free |
| `src/compiler.c` | `compileClassDecl`, `compileGet`, `compileSet`, `compileThis`, `compileSuper` |
| `src/vm.c` | `OP_CLASS`, `OP_GET_PROPERTY`, `OP_SET_PROPERTY`, `OP_METHOD`, `OP_INHERIT`, `OP_INVOKE`, `OP_SUPER_INVOKE`, `OP_GET_SUPER` |

## TDD Cycles

### Cycle 1: Object System Extensions (MsClass, MsInstance, MsBoundMethod)

**RED**: `ms_class_new`, `ms_instance_new`, `ms_bound_method_new` undefined → link error.

- `test_class_object()`: `ms_class_new(vm, name)` → type `MS_OBJ_CLASS`, methods table init'd, `superclass == NULL`
- `test_instance_object()`: `ms_instance_new(vm, klass)` → type `MS_OBJ_INSTANCE`, fields table init'd, `klass` matches
- `test_bound_method_object()`: `ms_bound_method_new(vm, receiver, method)` → type `MS_OBJ_BOUND_METHOD`, receiver + method match
- `test_type_macros()`: `MS_IS_CLASS`/`MS_IS_INSTANCE`/`MS_AS_CLASS`/`MS_AS_INSTANCE` → correct
- `test_class_with_super()`: class with superclass → `superclass` pointer correct
- `test_object_free_chain()`: class + instance + bound method → free VM → no leak

**GREEN**:
- `src/object.h`: `MS_IS_CLASS`/`MS_IS_INSTANCE`, `MS_AS_CLASS`/`MS_AS_INSTANCE`
- `src/object.c`:
  - `ms_class_new(vm, name)`: allocate, set name, `ms_table_init(&methods)`, `superclass = NULL`
  - `ms_instance_new(vm, klass)`: allocate, set klass, `ms_table_init(&fields)`
  - `ms_bound_method_new(vm, receiver, method)`: allocate, set receiver + method (closure)
  - `ms_object_free()`: CLASS → free methods table; INSTANCE → free fields table; BOUND_METHOD → free (closure GC-managed)
  - `ms_gc_blacken_object()`: CLASS → mark name + superclass + methods values; INSTANCE → mark klass + fields values; BOUND_METHOD → mark receiver + method

**Verify GREEN**: `cmake --build build && ./build/test_classes`

**REFACTOR**: Class/instance table entries properly marked during GC.

### Cycle 2: Class Declaration Compilation

**RED**: `compileClassDecl` not implemented → compiler error.

- `test_compile_empty_class()`: `"class Foo {}"` → `OP_CLASS("Foo")`, `OP_DEFINE_GLOBAL("Foo")`
- `test_compile_class_with_method()`: `"class Foo { fn bar() { print 1 } }"` → `OP_CLASS`, `OP_DEFINE_GLOBAL`, `OP_GET_GLOBAL("Foo")`, `OP_CLOSURE(method_fn)`, `OP_METHOD("bar")`
- `test_compile_class_with_superclass()`: `"class Child < Parent {}"` → `OP_CLASS`, `OP_DEFINE_GLOBAL`, `OP_GET_GLOBAL("Parent")`, `OP_GET_GLOBAL("Child")`, `OP_INHERIT`
- `test_compile_init_method()`: `"class Foo { fn init() { this.x = 1 } }"` → method compiled as `MS_FUNC_INITIALIZER`

**GREEN**: Replace stub `compileClassDecl()`:
1. Consume `class` + identifier
2. Emit `OP_CLASS` + `OP_DEFINE_GLOBAL`
3. If `<`: consume superclass name → emit `OP_GET_GLOBAL(super)`, `OP_GET_GLOBAL(class)`, `OP_INHERIT`
4. Consume `{`
5. `beginScope()`: declare `this` local (slot 0); if superclass, declare `super` local (slot 1)
6. Loop methods: consume `fn` + name → new `MsCompilerState` (`MS_FUNC_METHOD` or `MS_FUNC_INITIALIZER` if "init") → compile body → `OP_CLOSURE` → `OP_METHOD(name)`
7. Consume `}`
8. `endScope()`

- Methods capture `this` as upvalue if accessed in nested fns

**Verify GREEN**: build + run → class compilation tests pass.

**REFACTOR**: Share fn compilation logic between `compileFuncDecl` and method compilation.

### Cycle 3: Instance Creation & Field Access

**RED**: VM doesn't handle instantiation or field access.

- `test_instantiate_class()`: `"class Foo {}\nvar f = Foo()"` → `MS_INTERPRET_OK`
- `test_set_field()`: `"class Foo {}\nvar f = Foo()\nf.x = 42\nprint f.x"` → "42"
- `test_get_field()`: set then retrieve → value matches
- `test_set_field_wrong_type()`: `"var x = 1\nx.y = 2"` → runtime error "only instances have fields"
- `test_get_field_undefined()`: `"class Foo {}\nvar f = Foo()\nprint f.x"` → runtime error "undefined property"

**GREEN**:
- `OP_CLASS`: create `MsClass`, push
- `callValue()` for class: create `MsInstance`, push as receiver; if has `init` → call it; else just push instance
- `OP_GET_PROPERTY`:
  1. Pop instance (verify instance, else error)
  2. Read name constant
  3. Lookup `instance->fields` → found → push value
  4. Lookup `instance->klass->methods` → found → create `MsBoundMethod`, push
  5. Neither → runtime error "undefined property"
- `OP_SET_PROPERTY`: pop instance + name + value → set in `instance->fields` → push value
- `src/compiler.c`: `compileGet()` → `.name` → `OP_GET_PROPERTY`; `compileSet()` → `.name = expr` → `OP_SET_PROPERTY`

**Verify GREEN**: `cmake --build build && ./build/test_classes`

**REFACTOR**: Consolidate property lookup (fields → methods) into helper.

### Cycle 4: Method Definitions & Invocation

**RED**: Method calls not working.

- `test_method_call()`: `"class Foo { fn bar() { print \"hello\" } }\nvar f = Foo()\nf.bar()"` → "hello"
- `test_method_with_args()`: `"class Adder { fn add(a, b) { return a + b } }\nvar a = Adder()\nprint a.add(3, 4)"` → "7"
- `test_method_access_field()`: `"class Foo { fn greet() { print this.name } fn init(n) { this.name = n } }\nvar f = Foo(\"world\")\nf.greet()"` → "world"
- `test_method_not_found()`: `"class Foo {}\nFoo().bar()"` → runtime error "undefined method"
- `test_invoke_opcode()`: `"f.greet()"` → can emit `OP_INVOKE` (direct optimization)

**GREEN**:
- `OP_METHOD`: pop closure + class → store in `class->methods[name]`
- `OP_INVOKE` (optimization): read name + argCount → pop receiver (verify instance) → lookup `instance->klass->methods` → found → call directly; not found → error
- `invokeFromClass(klass, name, argCount)`: find method → bind receiver → create call frame
- `bindMethod(vm, klass, name)`: lookup → create `MsBoundMethod` with stack top as receiver → push
- `src/compiler.c`: `.` + identifier + `(` → `OP_INVOKE` instead of `OP_GET_PROPERTY` + `OP_CALL`

**Verify GREEN**: `cmake --build build && ./build/test_classes`

**REFACTOR**: `OP_INVOKE` and `OP_GET_PROPERTY` + `OP_CALL` produce same behavior.

### Cycle 5: Constructors (init)

**RED**: Constructor not properly invoked.

- `test_init_constructor()`: `"class Foo { fn init() { this.x = 1 } }\nvar f = Foo()\nprint f.x"` → "1"
- `test_init_with_args()`: `"class Foo { fn init(x) { this.x = x } }\nvar f = Foo(42)\nprint f.x"` → "42"
- `test_init_returns_self()`: `Foo()` returns instance (init return value discarded)
- `test_init_implicit()`: class without init → `"class Foo {}\nvar f = Foo()"` → no error
- `test_init_not_callable_directly()`: `f.init()` → calls init again

**GREEN**:
- `callValue()` for class:
  1. Create `MsInstance`, push as receiver
  2. Lookup `init` in `class->methods`
  3. Found → call (new frame, `this` = instance)
  4. Not found → just push instance
  5. After init returns → instance remains on stack (init return discarded)
- `OP_RETURN` for `MS_FUNC_INITIALIZER`: returns `this` (receiver) not explicit return
- Compiler: method named "init" → `MS_FUNC_INITIALIZER` type

**Verify GREEN**: `cmake --build build && ./build/test_classes`

**REFACTOR**: Initializer methods always return `this`.

### Cycle 6: Inheritance & Super

**RED**: Inheritance and super not implemented.

- `test_basic_inheritance()`: `"class Base { fn greet() { print \"hi\" } }\nclass Child < Base {}\nChild().greet()"` → "hi"
- `test_method_override()`: `"class Base { fn greet() { print \"base\" } }\nclass Child < Base { fn greet() { print \"child\" } }\nChild().greet()"` → "child"
- `test_super_call()`: `"class Base { fn greet() { print \"base\" } }\nclass Child < Base { fn greet() { super.greet() } }\nChild().greet()"` → "base"
- `test_super_with_args()`: super method with args → args passed correctly
- `test_super_in_init()`: `"class Base { fn init() { this.x = 1 } }\nclass Child < Base { fn init() { super.init() } }\nvar c = Child()\nprint c.x"` → "1"
- `test_super_no_superclass()`: `"class Foo { fn bar() { super.bar() } }\nFoo().bar()"` → runtime error "no superclass"
- `test_super_chain()`: three-level inheritance → super.greet() calls correct level
- `test_get_super()`: `"var m = super.greet\nprint m()"` → "hi"

**GREEN**:
- `OP_INHERIT`: pop superclass + subclass → verify superclass is class → copy superclass methods into subclass (skip overridden) → set `subclass->superclass = superclass`
- `OP_GET_SUPER`: read name → pop superclass → lookup method → create bound method with receiver → push
- `OP_SUPER_INVOKE`: read name + argCount → pop superclass → lookup method → `invokeFromClass(superclass, name, argCount)`
- `src/compiler.c`:
  - `compileThis()`: `OP_GET_LOCAL` for `this` (slot 0)
  - `compileSuper()`: `OP_GET_LOCAL` for `super` (slot 1); if `.method()` → `OP_SUPER_INVOKE`; if `.method` → `OP_GET_SUPER`

**Verify GREEN**: `cmake --build build && ./build/test_classes`

**REFACTOR**: `OP_INHERIT` emitted at compile time so methods available at runtime.

### Cycle 7: this Binding & Integration Tests

**RED**: Edge cases in this/super binding, integration failures.

- `test_this_in_method()`: `f.who().name()` → "Foo"
- `test_this_in_nested_function()`: `this` captured in closure via upvalue
- `test_this_not_outside_class()`: `"print this"` → compile error
- `test_super_not_without_superclass()`: → runtime error
- `test_field_on_non_instance()`: `"var x = 1\nx.y = 2"` → runtime error
- Integration scripts:
  - `tests/classes/basic.ms`: creation, methods, fields
  - `tests/classes/inheritance.ms`: chain, super calls, override
  - `tests/classes/methods.ms`: method types, constructors, this
- Stress GC (`MS_DEBUG_STRESS_GC`) with class objects

**GREEN**:
- `this` = local slot 0 in method scope; captured as upvalue in nested closures
- Error handling: `this` outside class → compile error; field on non-instance → runtime error; `super` without superclass → runtime error
- Integration scripts → full pipeline → compare expected output
- Stress GC: `cmake -B build -DMS_DEBUG_STRESS_GC=ON && cmake --build build && ./build/test_classes`

**Verify GREEN**: all tests pass including integration + stress GC.

**REFACTOR**: Final review of class opcodes, consistent error messages.

## Acceptance Criteria

- [ ] `"class Foo { fn bar() { print \"hello\" } }\nvar f = Foo()\nf.bar()"` → "hello"
- [ ] `"class Foo { fn init() { this.x = 1 } }\nvar f = Foo()\nprint f.x"` → "1"
- [ ] `"class Foo {}\nvar f = Foo()\nf.x = 42\nprint f.x"` → "42"
- [ ] `"class Base { fn greet() { print \"hi\" } }\nclass Child < Base {}\nChild().greet()"` → "hi"
- [ ] `"class Base { fn greet() { print \"base\" } }\nclass Child < Base { fn greet() { super.greet() } }\nChild().greet()"` → "base"
- [ ] Method override: subclass replaces inherited method
- [ ] `this` binds correctly in methods
- [ ] Field access on non-instance → runtime error
- [ ] No memory leaks

## Notes

- Types: `MsClass` (name, methods table, superclass ptr), `MsInstance` (klass ptr, fields table), `MsBoundMethod` (receiver value, closure ptr)
- Opcodes: `OP_CLASS`, `OP_GET_PROPERTY`, `OP_SET_PROPERTY`, `OP_METHOD`, `OP_INHERIT`, `OP_INVOKE`, `OP_SUPER_INVOKE`, `OP_GET_SUPER`
- `OP_INHERIT` copies superclass methods into subclass at runtime on class declaration execution
- `OP_INVOKE` optimizes property get + call into single opcode (avoids intermediate bound method alloc)
- Initializer (`init`) always returns `this` regardless of explicit return
- `this`/`super` = locals (slots 0/1) in method scope, available for upvalue capture
