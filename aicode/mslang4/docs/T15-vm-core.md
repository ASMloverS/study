# T15: VM Core

**Phase**: 6 · **Deps**: T14 (Compiler — Basic), T09 (Hash Table) · **Complexity**: High

## Goal

Stack-based VM execution engine. Arithmetic, variables, control flow, print. Combined with T14 → source → scanner → parser → compiler → VM → output.

## Files

| File | Purpose |
|------|---------|
| `src/vm.h` | `MsVM` struct, call frame, interpret API |
| `src/vm.c` | Execution engine (basic ops) |

## TDD Cycles

### Cycle 1: VM Init/Free + Stack Operations

**RED**: `ms_vm_init`, `ms_vm_free` undefined → link error.

- `test_vm_init_free()`: init → `stackTop == stack`, `frameCount == 0`, `initialized == true`; free → no crash/leak
- `test_vm_push_pop()`: push value → `stackTop == stack + 1`; pop → value matches, `stackTop == stack`
- `test_vm_peek()`: push two → `peek(0)` = top, `peek(1)` = second

**GREEN**: Create `src/vm.h`:
```c
#ifndef MS_VM_H
#define MS_VM_H

#include "chunk.h"
#include "table.h"
#include "object.h"
#include "compiler.h"

typedef struct { MsClosure* closure; uint8_t* ip; MsValue* slots; } MsCallFrame;

typedef struct MsVM {
    MsValue stack[MS_STACK_MAX];
    MsValue* stackTop;
    MsCallFrame frames[MS_FRAMES_MAX];
    int frameCount;
    MsTable globals;
    MsTable strings;
    MsTable modules;
    char** modulePaths; int modulePathCount; int modulePathCapacity;
    MsObject* objects;
    size_t bytesAllocated;
    size_t nextGC;
    MsObject** grayStack; int grayCount; int grayCapacity;
    MsCompiler* compiler;
    bool initialized;
} MsVM;

typedef enum { MS_INTERPRET_OK, MS_INTERPRET_COMPILE_ERROR, MS_INTERPRET_RUNTIME_ERROR } MsInterpretResult;

void ms_vm_init(MsVM* vm);
void ms_vm_free(MsVM* vm);
MsInterpretResult ms_vm_interpret(MsVM* vm, const char* source);

#endif
```

Create `src/vm.c`:
- `ms_vm_init()`: `stackTop = stack`, `frameCount = 0`, `ms_table_init` for globals/strings/modules, `objects = NULL`, `bytesAllocated = 0`, `nextGC = 1MB`, `grayStack = NULL`, `compiler = NULL`, `modulePaths = NULL`, `initialized = true`
- `ms_vm_free()`: free all objects via `ms_object_free()`, free gray stack, free module paths, `ms_table_free` for globals/strings/modules, reset stack
- Static helpers: `push()`, `pop()`, `peek()`, `resetStack()`, `runtimeError()`
- Update `object.h`/`object.c`: `ms_alloc_object()` links into `vm->objects` list

**Verify GREEN**: `cmake --build build && ./build/test_vm`

**REFACTOR**: Ensure `resetStack()` reinitializes all stack-related state.

### Cycle 2: Compile + Interpret Literals

**RED**: `ms_vm_interpret` not implemented → wrong result or crash.

- `test_interpret_number()`: `ms_vm_interpret(vm, "42")` → `MS_INTERPRET_OK`
- `test_interpret_bool_nil()`: `"true"`, `"nil"` → `MS_INTERPRET_OK`
- `test_interpret_empty()`: `""` → `MS_INTERPRET_OK`

**GREEN**: Implement `ms_vm_interpret()`:
1. `ms_compiler_compile()` → compile source
2. NULL return → `MS_INTERPRET_COMPILE_ERROR`
3. Create `MsClosure` wrapping compiled function
4. Set up first call frame: `closure`, `ip = chunk.code`, `slots = vm.stack`
5. Call `run()`, return result

`run()` — switch dispatch loop:
- `MS_OP_CONSTANT`: read index → push constant
- `MS_OP_NIL`: push nil
- `MS_OP_TRUE` / `MS_OP_FALSE`: push bool
- `MS_OP_POP`: pop
- `MS_OP_RETURN`: return `MS_INTERPRET_OK` (top-level)

**Verify GREEN**: build + run → literal tests pass.

**REFACTOR**: Computed goto vs switch — defer optimization.

### Cycle 3: Arithmetic + Comparison Operations

**RED**: Opcodes not handled, output capture fails.

- `test_interpret_add()`: `"print 1 + 2"` → "3"
- `test_interpret_subtract()`: `"print 5 - 3"` → "2"
- `test_interpret_multiply()`: `"print 3 * 4"` → "12"
- `test_interpret_divide()`: `"print 10 / 2"` → "5"
- `test_interpret_modulo()`: `"print 10 % 3"` → "1"
- `test_interpret_negate()`: `"print -5"` → "-5"
- `test_interpret_not()`: `"print !true"` → "false"
- `test_interpret_comparison()`: `"print 1 < 2"` → "true", `"print 1 == 1"` → "true", `"print 1 != 2"` → "true"
- `test_interpret_type_error()`: `"print 1 + \"hello\""` → runtime error

**GREEN**: Extend `run()`:
- `MS_OP_ADD`: pop two; both numbers → result; both strings → concatenate; else → runtime error
- `MS_OP_SUBTRACT`/`MULTIPLY`/`DIVIDE`/`MODULO`: pop two numbers → compute; type mismatch → runtime error
- `MS_OP_NEGATE`: pop number → negate; non-number → runtime error
- `MS_OP_NOT`: pop → push boolean inverse
- `MS_OP_EQUAL`: pop two → push `ms_value_equal(a, b)`
- `MS_OP_NOT_EQUAL`: EQUAL + NOT
- `MS_OP_GREATER`/`LESS`/`GREATER_EQUAL`/`LESS_EQUAL`: pop two numbers → compare → push bool
- String concat: `ms_string_copy()` → new interned string
- Output capture for tests: callback or redirected stdout

**Verify GREEN**: build + run → arithmetic/comparison tests pass.

**REFACTOR**: Consolidate binary op dispatch via helper macro.

### Cycle 4: Variables (Global + Local)

**RED**: Global/local opcodes not handled.

- `test_interpret_global_var()`: `"var x = 42\nprint x"` → "42"
- `test_interpret_set_global()`: `"var x = 1\nx = 2\nprint x"` → "2"
- `test_interpret_local_var()`: `"{ var x = 42\nprint x }"` → "42"
- `test_interpret_scope()`: `"var x = 1\n{ var x = 2\nprint x }\nprint x"` → "2" then "1"
- `test_interpret_undefined_var()`: `"print undefined"` → `MS_INTERPRET_RUNTIME_ERROR`
- `test_interpret_set_undefined_var()`: `"undefined = 1"` → runtime error or implicit define

**GREEN**: Extend `run()`:
- `MS_OP_DEFINE_GLOBAL`: read name → pop value → `ms_table_set(&vm->globals, name, value)`
- `MS_OP_GET_GLOBAL`: read name → `ms_table_get`; not found → "undefined variable" runtime error
- `MS_OP_SET_GLOBAL`: read name → `ms_table_set`; pop + push value
- `MS_OP_GET_LOCAL`: read slot → push `frame->slots[slot]`
- `MS_OP_SET_LOCAL`: read slot → pop → store at `frame->slots[slot]`
- Call frame `slots` set to stack base for top-level script

**Verify GREEN**: build + run → variable tests pass.

**REFACTOR**: Verify scope push/pop manages stack slots correctly.

### Cycle 5: Control Flow (If/Else, While, For, Break/Continue)

**RED**: Jump opcodes not handled.

- `test_interpret_if_true()`: `"if (true) print 1 else print 2"` → "1"
- `test_interpret_if_false()`: `"if (false) print 1 else print 2"` → "2"
- `test_interpret_while()`: `"var i = 0\nwhile (i < 5) { print i\ni = i + 1 }"` → 0,1,2,3,4
- `test_interpret_for()`: `"for (var i = 0; i < 3; i = i + 1) print i"` → 0,1,2
- `test_interpret_break()`: while + break at i>=3 → 0,1,2
- `test_interpret_continue()`: for + continue at i==2 → 0,1,3,4
- `test_interpret_nested_loops()`: break/continue target correct loop

**GREEN**: Extend `run()`:
- `MS_OP_JUMP`: read 2-byte signed offset → `ip += offset`
- `MS_OP_JUMP_IF_FALSE`: read offset, pop; if falsy → `ip += offset`
- `MS_OP_LOOP`: read 2-byte signed offset → `ip -= offset` (backward)
- `MS_OP_AND`: read offset; if top falsy → skip; else pop + continue
- `MS_OP_OR`: read offset; if top truthy → skip; else pop + continue
- `MS_OP_BREAK` / `MS_OP_CONTINUE`: same as `MS_OP_JUMP` (compiler already emitted correct target)
- Jump encoding: 2 bytes, big-endian, signed 16-bit

**Verify GREEN**: build + run → all control flow tests pass.

**REFACTOR**: Extract `readShort()` helper for 2-byte offset.

### Cycle 6: String Operations + Interning

**RED**: String operations not working.

- `test_interpret_string_concat()`: `"print \"hello\" + \" \" + \"world\""` → "hello world"
- `test_interpret_string_var()`: `"var s = \"test\"\nprint s"` → "test"
- `test_interpret_string_comparison()`: `"print \"abc\" == \"abc\""` → "true"
- `test_string_interning()`: intern same string twice → same pointer (identity)

**GREEN**:
- String interning: `ms_string_copy()` checks `vm->strings`; if exists → return existing; else allocate + add to table
- `MS_OP_ADD` string concat: compute length → allocate buffer → copy both → create interned result
- String equality: interned → pointer compare; non-interned → `strcmp`
- `ms_value_print()`: strings with quotes (debug), without (print output)

**Verify GREEN**: build + run → string tests pass.

**REFACTOR**: Ensure all string allocations go through interning path.

### Cycle 7: Print + Runtime Errors + End-to-End Pipeline

**RED**: Print/runtime errors not clean.

- `test_interpret_print()`: `"print 42"` → "42\n"
- `test_interpret_print_string()`: `"print \"hello\""` → "hello\n"
- `test_interpret_print_bool()`: `"print true"` → "true\n"
- `test_interpret_runtime_error_stack_overflow()`: deep recursion → `MS_INTERPRET_RUNTIME_ERROR`
- `test_interpret_multiple_scripts()`: init → run script1 → run script2 → free; both succeed
- `test_interpret_full_program()`: `"var sum = 0\nfor (var i = 1; i <= 10; i = i + 1) sum = sum + i\nprint sum"` → "55"
- Integration tests:
  - `tests/basic/arithmetic.ms`: comprehensive arithmetic
  - `tests/basic/variables.ms`: variable lifecycle
  - `tests/basic/control_flow.ms`: if/while/for/break/continue
  - `tests/basic/strings.ms`: string operations

**GREEN**:
- `MS_OP_PRINT`: pop → `ms_value_print()` + newline
- `runtimeError()`: format message + line number → `resetStack()`, decrement `frameCount`
- Stack overflow: `call()` checks `frameCount >= MS_FRAMES_MAX`
- VM state between calls: reset stack, keep globals
- Integration: read `.ms` file → interpret → compare output

**Verify GREEN**: build + run → all tests pass including integration scripts.

**REFACTOR**: Clean output formatting; error messages include source line info.

## Acceptance Criteria

- [ ] `"print 1 + 2"` → outputs "3"
- [ ] `"var x = 42\nprint x"` → outputs "42"
- [ ] `"if (true) print 1 else print 2"` → outputs "1"
- [ ] while loop → outputs 0,1,2,3,4
- [ ] for loop → outputs 0,1,2
- [ ] Undefined variable → runtime error
- [ ] Stack overflow → runtime error
- [ ] VM init → multiple scripts → free, no leaks
- [ ] `print "hello" + " " + "world"` → "hello world"

## Notes

- Static helpers: `push`, `pop`, `peek`, `resetStack`, `runtimeError`
- `ms_vm_interpret()` flow: compile → set up call frame → `run()` → result
- Updates `object.h`/`object.c` for VM integration (`ms_alloc_object` links into `vm->objects`)
- String interning via `vm->strings` table
- GC fields (`bytesAllocated`, `nextGC`, `grayStack`, …) initialized here; actual GC → T17
