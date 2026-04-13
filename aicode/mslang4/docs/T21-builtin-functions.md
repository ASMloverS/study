# T21: Built-in Functions

**Phase**: 12 · **Deps**: T16 (Functions & Closures) · **Complexity**: Medium

## Goal

Built-in native functions: `print`, `clock`, `type`, `len`, `input`, `str`, `num`. Registered as MsNative in VM globals during init.

## Files

| File | Action |
|------|--------|
| `src/builtins.h` | Create: builtin declarations |
| `src/builtins.c` | Create: builtin implementations |
| `src/vm.c` | Modify: call `ms_builtins_define_all()` during init |

## TDD Cycles

### Cycle 1: Builtin Framework & `print`

**RED**: `src/builtins.h`/`src/builtins.c` don't exist → link error.

- `test_builtin_print_string`: `ms_builtin_print(vm, 1, args)` with string `"hello"` → stdout `"hello\n"`
- `test_builtin_print_number`: number 42.0 → `"42\n"`
- `test_builtin_print_nil`: nil → `"nil\n"`
- `test_builtins_define_all`: `ms_builtins_define_all(&vm)` → lookup "print" in globals → non-nil

**GREEN**:
- Create `src/builtins.h`:
  ```c
  #ifndef MS_BUILTINS_H
  #define MS_BUILTINS_H

  #include "vm.h"

  void ms_builtins_define_all(MsVM* vm);
  MsValue ms_builtin_print(MsVM* vm, int argCount, MsValue* args);
  MsValue ms_builtin_clock(MsVM* vm, int argCount, MsValue* args);
  MsValue ms_builtin_type(MsVM* vm, int argCount, MsValue* args);
  MsValue ms_builtin_len(MsVM* vm, int argCount, MsValue* args);
  MsValue ms_builtin_input(MsVM* vm, int argCount, MsValue* args);
  MsValue ms_builtin_str(MsVM* vm, int argCount, MsValue* args);
  MsValue ms_builtin_num(MsVM* vm, int argCount, MsValue* args);

  #endif
  ```
- Create `src/builtins.c`: `ms_builtins_define_all()` → for each builtin, create MsString name + MsNative via `ms_native_new()` → `ms_table_set()` in globals
- `ms_builtin_print`: print each arg via `ms_print_value()` to stdout + newline → return `ms_nil_val()`

**Verify GREEN**: `cmake --build build && build\test_builtins`

**REFACTOR**: argCount validation (print accepts any count, others fixed arity).

### Cycle 2: `clock` & `type` Builtins

**RED**: `ms_builtin_clock`/`ms_builtin_type` return nil (stub).

- `test_builtin_clock`: `ms_builtin_clock(vm, 0, NULL)` → number >= 0
- `test_builtin_type_number`: type(42.0) → MsString `"number"`
- `test_builtin_type_string`: type(string) → `"string"`
- `test_builtin_type_bool`: type(true) → `"bool"`
- `test_builtin_type_nil`: type(nil) → `"nil"`

**GREEN**:
- `ms_builtin_clock`: return `ms_number_val(ms_platform_get_time_seconds())`
- `ms_builtin_type`: inspect type tag → return MsString: `"nil"`/`"bool"`/`"number"`/`"string"`/`"function"`/`"class"`/`"instance"`/`"list"`

**Verify GREEN**: `cmake --build build && build\test_builtins`

**REFACTOR**: Lookup table for type names instead of if-else chain.

### Cycle 3: `len` Builtin

**RED**: `ms_builtin_len` not implemented.

- `test_builtin_len_string`: len("hello") → `ms_number_val(5.0)`
- `test_builtin_len_list`: MsList with 3 elements → `ms_number_val(3.0)`
- `test_builtin_len_invalid`: len(number) → runtime error

**GREEN**:
- `ms_builtin_len`: string → `string->length`; list → `list->count`; else → runtime error "len() expects string or list"

**Verify GREEN**: `cmake --build build && build\test_builtins`

**REFACTOR**: None.

### Cycle 4: `str` & `num` Conversion Builtins

**RED**: `ms_builtin_str`/`ms_builtin_num` not implemented.

- `test_builtin_str_number`: str(42.0) → MsString `"42"`
- `test_builtin_str_bool`: str(true) → `"true"`
- `test_builtin_num_string`: num("3.14") → `ms_number_val(3.14)`
- `test_builtin_num_number`: num(42) → same number
- `test_builtin_num_bool`: num(true) → `ms_number_val(1.0)`
- `test_builtin_num_invalid`: num(list) → runtime error

**GREEN**:
- `ms_builtin_str`: convert value via `ms_print_value()` output → wrap as MsString
- `ms_builtin_num`: string → `strtod()`; number → as-is; bool → 1.0/0.0; else → runtime error "num() expects string, number, or bool"

**Verify GREEN**: `cmake --build build && build\test_builtins`

**REFACTOR**: `ms_print_value()` formatting matches expected output (integer vs float).

### Cycle 5: `input` Builtin

**RED**: `ms_builtin_input` not implemented.

- `test_builtin_input_no_prompt`: pipe "hello world\n" to stdin → MsString `"hello world"`
- `test_builtin_input_with_prompt`: prompt arg printed to stdout (no newline), reads from piped stdin
- `test_builtin_input_strips_newline`: pipe "test\n" → no trailing `\n`

**GREEN**:
- `ms_builtin_input`: if argCount > 0 → print args[0] as prompt via `fputs` (no newline); `fgets()` from stdin → strip trailing `\n`/`\r\n` → return `ms_string_copy()`

**Verify GREEN**: `cmake --build build && build\test_builtins`

**REFACTOR**: Handle EOF, very long input, empty input.

### Cycle 6: Integration — Builtins Accessible from Maple Scripts

**RED**: `ms_builtins_define_all()` not called during VM init → "undefined variable 'print'".

- `tests/integration/test_builtins.ms`:
  ```
  print("hello")
  print(type(42))
  print(len("hello"))
  print(str(3.14))
  print(num("42") + 1)
  ```
  Expected: `hello` / `number` / `5` / `3.14` / `43`

**GREEN**:
- `src/vm.c` `ms_vm_init()`: call `ms_builtins_define_all(vm)` after globals table init
- Verify all 7 builtins registered: `print`, `clock`, `type`, `len`, `input`, `str`, `num`

**Verify GREEN**: `build\maple tests\integration\test_builtins.ms` → correct output

**REFACTOR**: Builtins registered before any user code runs (including module loading).

## Acceptance Criteria

- [ ] `print("hello")` → "hello\n"
- [ ] `clock()` → number >= 0
- [ ] `type(42)` → "number", `type("hi")` → "string", `type(true)` → "bool"
- [ ] `len("hello")` → 5, `len([1,2,3])` → 3
- [ ] `str(42)` → "42", `num("3.14")` → 3.14
- [ ] `input()` reads stdin (piped input)
- [ ] All builtins accessible without import

## Notes

- All builtins = MsNative objects in VM global table during init
- `print` uses `ms_print_value()` for consistent formatting
- `clock` depends on `ms_platform_get_time_seconds()` from platform layer
- `input` uses `fgets()` — platform layer may abstract further
- Each builtin validates arg count + types → clear runtime errors
