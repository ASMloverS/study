# Task 11 - Classes, Methods, self, super, Inheritance

## Goal

Implement the lightweight object system so classes, instances, methods, and
single inheritance run end to end.

## Design Links

- Object model, `self`/`super`, `init`, and class semantics:
  [../mslangc-design.md](../mslangc-design.md)
- Repository rules: [../AGENTS.md](../AGENTS.md)

## Dependencies

1. Task 09
2. Task 10

## Scope

1. Implement class, instance, and bound-method runtime objects.
2. Support field read/write, field-before-method lookup, and automatic `self`
   binding.
3. Support single inheritance and `super.method()` dispatch.
4. Support `init` initializer semantics.
5. Enforce `self`/`super` legality, `init` return restrictions, and
   self-inheritance checks.

## Implementation Boundaries

1. This task owns class runtime behavior and the resolver/lowering hooks for
   class semantics.
2. It must reuse the unified runtime object header rather than create a
   parallel object model.
3. Runtime property and method failures are runtime errors, not resolver
   errors.
4. Property assignment mutates instance fields only; it must not overwrite
   class method tables.
5. Module loading, containers, and GC optimization stay outside this task.

## File Ownership

1. Runtime object type and allocation updates in `include/ms/object.h`,
   `include/ms/runtime/function.h`, `src/runtime/object.c`,
   `src/runtime/function.c`, `src/runtime/value.c`, and `src/runtime/vm.c`.
2. Class resolver and resolution metadata in `src/frontend/resolver.c` and
   `src/frontend/resolution_table.c`.
3. Class lowering support in `src/frontend/lowering_basic.c`.
4. Test registration in `tests/CMakeLists.txt` and `tests/unit/CMakeLists.txt`.
5. Resolver scripts under `tests/ms/resolver/`.
6. End-to-end class scripts under `tests/e2e/class/`.
7. Unit and feature tests in `tests/unit/resolver_test.c`,
   `tests/unit/lowering_basic_test.c`, and `tests/unit/classes_methods_test.c`.

## Diagnostics Contract

1. `self` outside class method context is `phase=resolve` with `MS3xxx`.
2. `super` outside subclass method context is `phase=resolve` with `MS3xxx`.
3. `return <expr>` inside `init` is `phase=resolve` with `MS3xxx`.
4. A class inheriting from itself is `phase=resolve` with `MS3xxx`.
5. Missing properties, invalid property receivers, and invalid method dispatch
   paths use `phase=runtime` with `MS4xxx`.

## Execution Breakdown

Every subtask must start with failing tests, then the minimal implementation,
then the focused verification command listed for that subtask.

### Subtask 11.1 - Resolver legality for class context

**Depends on:** Task 09 resolver infrastructure.

**Deliverables**

1. Track class, subclass, and initializer context in the resolver.
2. Allow `self` only inside a method body or a nested `fn` inside that method.
3. Allow `super` only inside subclass methods or nested `fn` inside those
   methods.
4. Reject `return <expr>` inside `init` while still allowing bare `return`.
5. Reject `class Foo < Foo {}` before lowering runs.

**Tests to add or update**

1. Extend `tests/unit/resolver_test.c` with positive coverage for method-local
   `self` and subclass-local `super` bindings.
2. Add `tests/ms/resolver/illegal_self_outside_class.ms`.
3. Add `tests/ms/resolver/illegal_super_outside_subclass.ms`.
4. Add `tests/ms/resolver/illegal_init_return_value.ms`.
5. Add `tests/ms/resolver/class_inherits_from_itself.ms`.
6. Register CLI tests in `tests/CMakeLists.txt` with names
   `resolver_static.illegal_self`, `resolver_static.illegal_super`,
   `resolver_static.illegal_init_return`, and `resolver_static.self_inherit`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "resolver\.unit|resolver_static\.(illegal_self|illegal_super|illegal_init_return|self_inherit)"
```

**Done when**

1. Resolver rejects all four invalid forms with `phase=resolve`.
2. Positive resolver tests prove nested closures can still capture `self` and
   `super`.

### Subtask 11.2 - Runtime class, instance, and bound-method objects

**Depends on:** Subtask 11.1.

**Deliverables**

1. Define runtime storage for classes, instances, and bound methods on top of
   the existing `MsObject` header.
2. Add constructors, destructors, and basic helpers for class name, method
   table, instance field table, and bound receiver storage.
3. Keep object-type names, printing hooks, and value helpers consistent with
   the existing runtime object model.

**Tests to add or update**

1. Add `tests/unit/classes_methods_test.c` for class allocation, instance field
   table initialization, and bound-method receiver storage.
2. Register a unit target named `classes_methods.unit` in
   `tests/unit/CMakeLists.txt`.
3. Extend `tests/unit/value_test.c` only if value-level predicates need class
   object assertions.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "classes_methods\.unit|runtime_core\.(value|string|table)"
```

**Done when**

1. The runtime can allocate and free class-related objects without special-case
   leaks or type mismatches.
2. Unit tests can inspect class name, superclass pointer, bound receiver, and
   empty field or method tables directly.

### Subtask 11.3 - Lower class declarations and install method tables

**Depends on:** Subtask 11.2.

**Deliverables**

1. Lower class declarations into the class-related opcodes from the design doc.
2. Define the class name in module scope before installing methods so methods
   can refer to the class inside their bodies.
3. Emit method installation in declaration order and preserve method names for
   later property lookup.

**Tests to add or update**

1. Extend `tests/unit/lowering_basic_test.c` with class-declaration opcode
   coverage.
2. Add `tests/e2e/class/basic_class.ms` and
   `tests/e2e/class/basic_class.ms.out`.
3. Register `class.basic_class` in `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "lowering_basic\.unit|class\.basic_class"
```

**Done when**

1. A class declaration compiles without fallback or unsupported-feature
   diagnostics.
2. The runtime can create a class object and expose it through the current
   module namespace.

### Subtask 11.4 - Instance fields and field-before-method lookup

**Depends on:** Subtask 11.3.

**Deliverables**

1. Implement instance property writes so `instance.name = value` stores into
   the instance field table only.
2. Implement property reads with lookup order `instance field -> class method ->
   superclass chain`.
3. Keep class method tables immutable through instance property assignment.

**Tests to add or update**

1. Add `tests/e2e/class/fields_and_methods.ms` and sidecar output.
2. Add `tests/e2e/class/field_shadows_method.ms` and sidecar output.
3. Extend `tests/unit/classes_methods_test.c` with direct field and method
   lookup assertions if runtime helpers are exposed.
4. Register `class.fields_and_methods` and `class.field_shadows_method` in
   `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "classes_methods\.unit|class\.(fields_and_methods|field_shadows_method)"
```

**Done when**

1. Field writes are visible on later reads from the same instance.
2. A field with the same name as a method shadows that method for that instance
   only.

### Subtask 11.5 - Bound methods, implicit `self`, and `init`

**Depends on:** Subtask 11.4.

**Deliverables**

1. Return a bound method when a method is read from an instance.
2. Inject the receiver as `self` when invoking a bound method.
3. Make classes callable as constructors that allocate an instance first.
4. Auto-invoke `init` when present and always return the new instance from a
   class call.
5. Preserve bare `return` in `init` as an early exit, not as a replacement
   return value.

**Tests to add or update**

1. Add `tests/e2e/class/bound_method.ms` and sidecar output.
2. Add `tests/e2e/class/init_returns_instance.ms` and sidecar output.
3. Extend `tests/unit/classes_methods_test.c` with compile-and-run coverage for
   method calls that read and write through `self`.
4. Register `class.bound_method` and `class.init_returns_instance` in
   `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "classes_methods\.unit|class\.(bound_method|init_returns_instance)"
```

**Done when**

1. Method calls observe the correct receiver through `self`.
2. Calling a class returns an instance even if `init` executes a bare `return`.
3. Resolver and runtime behavior together enforce that `init` cannot return an
   arbitrary explicit value.

### Subtask 11.6 - Single inheritance and `super.method()` dispatch

**Depends on:** Subtask 11.5.

**Deliverables**

1. Store an optional superclass on class objects.
2. Implement inheritance setup so subclasses see inherited methods unless they
   override them.
3. Lower `super.method()` using the resolver metadata from Subtask 11.1.
4. Bind `super` as a synthetic local that nested closures inside subclass
   methods may still capture.

**Tests to add or update**

1. Add `tests/e2e/class/inheritance_override.ms` and sidecar output.
2. Add `tests/e2e/class/super_dispatch.ms` and sidecar output.
3. Extend `tests/unit/resolver_test.c` with positive `super` capture coverage.
4. Extend `tests/unit/lowering_basic_test.c` with `GET_SUPER`,
   `SUPER_INVOKE`, or equivalent opcode coverage.
5. Register `class.inheritance_override` and `class.super_dispatch` in
   `tests/CMakeLists.txt`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "resolver\.unit|lowering_basic\.unit|class\.(inheritance_override|super_dispatch)"
```

**Done when**

1. Subclass overrides beat inherited methods on normal lookup.
2. `super.method()` reaches the superclass implementation while keeping the same
   receiver instance.

### Subtask 11.7 - Negative runtime diagnostics and full regression

**Depends on:** Subtask 11.6.

**Deliverables**

1. Cover missing-property failures and invalid property receiver paths with
   stable runtime diagnostics.
2. Make sure new class tests are wired into CMake and script runners.
3. Run the full class-focused acceptance sweep and fix remaining integration
   gaps before marking Task 11 complete.

**Tests to add or update**

1. Add `tests/e2e/class/missing_property.ms`.
2. Add `tests/e2e/class/invalid_property_receiver.ms`.
3. Register `class.missing_property` and `class.invalid_property_receiver` in
   `tests/CMakeLists.txt` with `EXPECT_EXIT=70`.
4. Keep at least one passing script that exercises the full chain
   `class -> init -> method -> override -> super`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "resolver\.unit|lowering_basic\.unit|classes_methods\.unit|class\.|resolver_static\.(illegal_self|illegal_super|illegal_init_return|self_inherit)"
build\Debug\mslangc.exe tests\e2e\class\super_dispatch.ms
```

**Done when**

1. Runtime failures report `phase=runtime` and stable `MS4xxx` codes.
2. All Task 11 tests pass from CTest and at least one representative `.ms`
   script runs directly through the CLI.

## Acceptance

1. All subtasks above are complete in order.
2. Classes can be instantiated and instance fields can be read and written.
3. Method calls bind `self` correctly.
4. Subclass override plus `super.method()` works.
5. Instance fields shadow methods on lookup, and class method tables remain
   immutable through instance property assignment.
6. The task is not complete until build passes, tests pass, `.ms` scripts run
   end to end, and all edited files are UTF-8 with LF and no trailing
   whitespace.

## Acceptance Commands

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "resolver\.unit|lowering_basic\.unit|classes_methods\.unit|class\.|resolver_static\.(illegal_self|illegal_super|illegal_init_return|self_inherit)"
build\Debug\mslangc.exe tests\e2e\class\super_dispatch.ms
```

## Out of Scope

1. Modules.
2. Containers.
3. GC tuning beyond the object lifecycle required for class support.
