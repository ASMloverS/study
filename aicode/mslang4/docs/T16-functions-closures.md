# T16: Functions & Closures

**Phase**: 7 · **Deps**: T15 (VM Core) · **Complexity**: High

## Goal

First-class functions, closures with upvalues, native function binding. Extends compiler, object system, and VM.

## Files

| File | Changes |
|------|---------|
| `src/object.h` | Fully implement `MsFunction`, `MsClosure`, `MsUpvalue`, `MsNative` |
| `src/object.c` | Creation + free for all new types |
| `src/compiler.c` | `compileFuncDecl`, upvalue resolution, `OP_CLOSURE` emission |
| `src/vm.c` | `call`/`callValue`, `OP_CALL`, `OP_CLOSURE`, `OP_CLOSE_UPVALUE`, upvalue mgmt |

## TDD Cycles

### Cycle 1: Object System Extensions

**RED**: `ms_function_new`, `ms_closure_new`, `ms_upvalue_new`, `ms_native_new` undefined → link error.

- `test_function_object()`: `ms_function_new(vm, name)` → type `MS_OBJ_FUNCTION`, chunk init'd, name matches
- `test_closure_object()`: `ms_closure_new(vm, function)` → type `MS_OBJ_CLOSURE`, function ptr matches, upvalue array init'd
- `test_upvalue_object()`: `ms_upvalue_new(vm, &slot)` → type `MS_OBJ_UPVALUE`, location = &slot
- `test_native_object()`: `ms_native_new(vm, fn, name, arity)` → type `MS_OBJ_NATIVE`, arity matches
- `test_type_macros()`: `MS_IS_FUNCTION`/`MS_IS_CLOSURE`/`MS_IS_NATIVE` → true; `MS_AS_*` casts correctly
- `test_object_free_chain()`: function + closure + upvalue → free VM → no leak

**GREEN**:
- `src/object.h`: add `MS_IS_FUNCTION`/`MS_IS_CLOSURE`/`MS_IS_NATIVE`/`MS_IS_UPVALUE`, `MS_AS_*` macros
- `src/object.c`:
  - `ms_function_new(vm, name)`: allocate, `ms_chunk_init()`, set name
  - `ms_closure_new(vm, function)`: allocate with upvalue array of `function->upvalueCount` entries
  - `ms_upvalue_new(vm, slot)`: allocate, `location = slot`, `closed = NIL`, `next = NULL`
  - `ms_native_new(vm, fn, name, arity)`: allocate, set fn ptr, name, arity
  - `ms_object_free()`: handle `FUNCTION`/`CLOSURE`/`UPVALUE`/`NATIVE`
- `MsFunction` fields: `MsChunk chunk`, `int arity`, `int upvalueCount`, `MsString* name`

**Verify GREEN**: `cmake --build build && ./build/test_functions`

**REFACTOR**: Verify all types use consistent `ms_alloc_object()` pattern.

### Cycle 2: Function Declaration Compilation

**RED**: `compileFuncDecl` not implemented → compiler error.

- `test_compile_function()`: `"fn add(a, b) { return a + b }\nprint add(1, 2)"` → `OP_CLOSURE` + upvalue operands, `OP_DEFINE_GLOBAL("add")`, `OP_CONSTANT(1)`, `OP_CONSTANT(2)`, `OP_CALL(2)`, `OP_PRINT`
- `test_compile_function_no_params()`: `"fn foo() { print 1 }"` → `OP_CLOSURE`, arity 0
- `test_compile_nested_function()`: nested fn decl → `OP_CLOSURE` with upvalue operands

**GREEN**: Replace stub `compileFuncDecl()`:
1. Consume `fn` + identifier (name)
2. Create new `MsCompilerState` linked to enclosing
3. Create `MsFunction` with name, `type = MS_FUNC_FUNCTION`
4. Declare name as local in enclosing scope
5. Begin scope, declare params as locals
6. Compile body (block)
7. Emit `OP_NIL` + `OP_RETURN` (implicit return)
8. Emit `OP_CLOSURE` + constant index
9. For each upvalue: emit byte (bit 0 = isLocal, remaining = index)
10. Define as variable in enclosing scope

- `addUpvalue(state, index, isLocal)`: dedup, add if new, return index
- `resolveUpvalue(state, name)`: walk enclosing states; found as local → local upvalue; found as upvalue → non-local upvalue; return index or -1

**Verify GREEN**: build + run → function compilation tests pass.

**REFACTOR**: Verify upvalue operand encoding for nested closures.

### Cycle 3: Function Calls in VM

**RED**: `OP_CALL` not handled.

- `test_call_simple_function()`: `"fn add(a, b) { return a + b }\nprint add(1, 2)"` → "3"
- `test_call_no_args()`: `"fn greet() { print \"hi\" }\ngreet()"` → "hi"
- `test_call_arity_error()`: `"fn foo(a) {}\nfoo(1, 2)"` → `MS_INTERPRET_RUNTIME_ERROR`
- `test_call_non_callable()`: `"var x = 1\nx()"` → runtime error
- `test_call_stack_overflow()`: deep recursion → runtime error

**GREEN**:
- `OP_CLOSURE`: read constant index → create `MsClosure`; read upvalue operands → populate (local: `captureUpvalue(vm, &frame->slots[idx])`; non-local: copy from enclosing closure)
- `OP_CALL`: read argCount → `callValue(peek(argCount), argCount)`
- `call(closure, argCount)`: check arity, check `frameCount < MS_FRAMES_MAX`, set up frame: `closure`, `ip = chunk.code`, `slots = stackTop - argCount - 1`
- `callValue(callee, argCount)`: closure → `call()`; native → execute + push result; else → runtime error
- `OP_RETURN`: close upvalues for current frame, pop frame, push return value on caller stack, return from `run()` if top-level
- Native fn type: `typedef MsValue (*MsNativeFn)(MsVM* vm, int argCount, MsValue* args)`

**Verify GREEN**: build + run → call tests pass.

**REFACTOR**: Verify frame slot pointers (callee stack starts at receiver position).

### Cycle 4: Return Values + Recursion

**RED**: Incorrect return handling.

- `test_return_value()`: `"fn double(x) { return x * 2 }\nprint double(5)"` → "10"
- `test_implicit_return_nil()`: `"fn noop() {}\nvar x = noop()\nprint x"` → "nil"
- `test_return_from_middle()`: `"fn abs(x) { if (x < 0) return -x\nreturn x }\nprint abs(-3)"` → "3"
- `test_recursion_fib()`: `"fn fib(n) { if (n <= 1) return n\nreturn fib(n-1) + fib(n-2) }\nprint fib(10)"` → "55"
- `test_recursion_factorial()`: `"fn fact(n) { if (n <= 1) return 1\nreturn n * fact(n-1) }\nprint fact(5)"` → "120"

**GREEN**: Ensure `OP_RETURN`:
- Top-level (`MS_FUNC_SCRIPT`): pop frame → `MS_INTERPRET_OK`
- Otherwise: capture return value → `closeUpvalues(frame->slots)` → pop frame → discard locals → push return value
- `compileFuncDecl()`: implicit `OP_NIL` + `OP_RETURN` at end if no explicit return

**Verify GREEN**: build + run → return/recursion tests pass.

**REFACTOR**: Verify implicit return doesn't double-emit if last stmt is already return.

### Cycle 5: Upvalue Capture + Closures

**RED**: Closures don't see enclosing variables.

- `test_closure_capture()`: `"fn makeCounter() { var count = 0\nfn inc() { count = count + 1\nreturn count }\nreturn inc }\nvar c = makeCounter()\nprint c()\nprint c()"` → "1" then "2"
- `test_closure_multiple()`: two closures sharing captured var → both see mutations
- `test_nested_closure()`: closure capturing from two levels up → correct value
- `test_closure_outlive_scope()`: returned closure works after enclosing fn returns
- `test_upvalue_close()`: `"fn f() { var x = 1\n{ var y = 2\nreturn x + y }\n}\nprint f()"` → "3"

**GREEN**:
- `captureUpvalue(vm, slot)`: walk `vm->openUpvalues`; exists → return it; else create new, insert sorted, return
- `closeUpvalues(vm, last)`: walk open list; for each at/above `last`: move value to `closed`, `location = &closed`, remove from list
- `OP_CLOSE_UPVALUE`: pop → close upvalue at stack top
- `OP_CLOSURE` upvalue population: local → `captureUpvalue(vm, &frame->slots[idx])`; non-local → copy from enclosing closure
- `vm->openUpvalues`: linked list sorted by stack slot (descending)
- `resolveUpvalue()` in compiler: recursive search; local → `isLocal=true`; upvalue → `isLocal=false`

**Verify GREEN**: build + run → closure tests pass.

**REFACTOR**: Verify open upvalues list sorted correctly for efficient lookup.

### Cycle 6: Native Function Binding

**RED**: No native function mechanism.

- `test_native_clock()`: native `clock()` → result is number
- `test_native_custom()`: native `double(x)` returning `x * 2` → `double(5)` outputs "10"
- `test_native_string_ops()`: native `strLen(s)` → `strLen("hello")` outputs "5"
- `test_native_arity_error()`: wrong arg count → runtime error

**GREEN**:
- `defineNative(vm, name, fn, arity)`: create `MsNative` → store in globals
- `callValue()`: `MS_OBJ_NATIVE` → check arity → call `native->function(vm, argCount, args)` → push result
- Signature: `MsValue (*MsNativeFn)(MsVM*, int, MsValue*)`
- `ms_vm_init()`: define standard natives (`clock`, etc.)
- Test helpers for registering custom natives

**Verify GREEN**: build + run → native tests pass.

**REFACTOR**: Move native definitions to `natives.c`/`natives.h`.

### Cycle 7: Integration Tests

**RED**: Integration scripts produce wrong output.

- `tests/functions/basic.ms`: decls, calls, returns, params
- `tests/functions/closures.ms`: capture, shared state, nested closures
- `tests/functions/recursion.ms`: fibonacci, factorial

**GREEN**: Run each `.ms` through full pipeline → compare against `.ms.expected`. Fix edge cases. Ensure `ms_compiler_mark_roots()` marks function objects (prevents premature GC collection before T17).

**Verify GREEN**: build + run → all integration tests pass.

**REFACTOR**: Verify all function/closure paths robust.

## Acceptance Criteria

- [ ] `"fn add(a, b) { return a + b }\nprint add(1, 2)"` → "3"
- [ ] `"fn fib(n) { if (n <= 1) return n\nreturn fib(n-1) + fib(n-2) }\nprint fib(10)"` → "55"
- [ ] Closure: `makeCounter` pattern → "1" then "2"
- [ ] Upvalues capture + mutate enclosing variables
- [ ] Native functions definable + callable
- [ ] Wrong arg count → runtime error
- [ ] Deep recursion → stack overflow runtime error
- [ ] No leaks with closures/upvalues

## Notes

- Object types: `MsFunction` (`MsChunk chunk`, `int arity`, `int upvalueCount`, `MsString* name`), `MsClosure` (`MsFunction*`, `MsUpvalue**`), `MsUpvalue` (`MsValue* location`, `MsValue closed`, `MsUpvalue* next`), `MsNative` (`MsNativeFn`, `MsString* name`, `int arity`)
- Open upvalues: linked list on VM, sorted by stack slot descending, for efficient closing
- `ms_compiler_mark_roots()` marks compiler state's function + constants as GC roots
- `OP_CLOSURE` operands: one byte per upvalue — bit 0 = isLocal, remaining = index
