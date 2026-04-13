# T23: Error Handling Polish

**Phase**: 13 · **Deps**: T22 (Main Entry Point & REPL) · **Complexity**: Medium

## Goal

Polish error handling: compile errors with source context + line/column, runtime errors with stack traces, import errors with clear messages. Colored output on terminals.

## Files

| File | Changes |
|------|---------|
| `src/compiler.c` | Source context in error messages |
| `src/vm.c` | Stack trace printing for runtime errors |
| `src/scanner.c` | Accurate position info on error tokens |
| `src/parser.c` | Source line in error output |
| `src/module.c` | Clear import error messages |

## TDD Cycles

### Cycle 1: Compile Errors with Line & Column

**RED**: Compile errors missing column info.

- `tests/errors/undefined_var.ms`: `var x = 1\nprint y` → stderr must contain line `2`, column, `Undefined variable 'y'`

**GREEN**:
- `src/scanner.c`: error tokens store accurate `line` and `column` (byte offset from line start)
- `src/parser.c`: store current source line text in parser struct
- `src/compiler.c` error format: `error: line 3, column 10: Undefined variable 'x'`

**Verify GREEN**: `build\maple tests\errors\undefined_var.ms 2>&1` → `line 2, column 8: Undefined variable 'y'`

**REFACTOR**: Column counting handles tabs consistently.

### Cycle 2: Source Context & Pointer in Compile Errors

**RED**: No source line or `^` pointer in error output.

- `tests/errors/syntax_error_context.ms`: `var x = 1\nprint x +\nvar y = 2` → stderr must show:
  ```
  error: line 2, column 11: Expected expression
      print x +
              ^
  ```

**GREEN**:
- `src/parser.c`: extract + store current source line text; include on error
- `src/compiler.c`: print source line, then spaces to column + `^`
- Error struct from DESIGN.md §4:
  ```c
  typedef struct {
      char message[256];
      MsToken token;
      char sourceLine[512];
  } MsCompileError;
  ```

**Verify GREEN**: `build\maple tests\errors\syntax_error_context.ms 2>&1` → source line + `^` pointer

**REFACTOR**: Clip long lines (context window around error column).

### Cycle 3: Colored Error Output

**RED**: No ANSI color codes in error output.

- Assert stderr contains ANSI escapes for red on "error:" when terminal; disabled when piped.

**GREEN**:
- Color helpers in `src/common.h` or `src/terminal.h`:
  ```c
  #define MS_COLOR_RED     "\033[31m"
  #define MS_COLOR_YELLOW  "\033[33m"
  #define MS_COLOR_RESET   "\033[0m"
  ```
- `ms_platform_is_terminal(FILE* stream)` → check terminal
- Compile errors: wrap "error:" in `MS_COLOR_RED` when stderr is terminal
- Runtime errors: error text in red, stack trace frames in yellow

**Verify GREEN**: `build\maple tests\errors\undefined_var.ms 2>&1` → contains `\033[31m` before "error:"

**REFACTOR**: Abstract color output into helper that checks terminal + applies color.

### Cycle 4: Runtime Error Stack Traces

**RED**: Runtime errors show message only, no stack trace.

- `tests/errors/runtime_stack_trace.ms`:
  ```
  fun foo() { var x = unknown }
  fun bar() { foo() }
  bar()
  ```
  → stderr must show:
  ```
  Runtime error: Undefined variable 'unknown'.
    in function 'foo' at runtime_stack_trace.ms:2:8
    in function 'bar' at runtime_stack_trace.ms:5:4
    in script at runtime_stack_trace.ms:7:1
  ```

**GREEN**:
- `src/vm.c`: walk call frames top→bottom:
  - Each frame: function name from closure, line from chunk line info via frame IP
  - Print `  in function 'name' at file:line:col` or `  in script at file:line:col`
- Error struct from DESIGN.md §4:
  ```c
  typedef struct {
      char message[256];
      MsCallFrame* frames[MS_FRAMES_MAX];
      int frameCount;
  } MsRuntimeError;
  ```

**Verify GREEN**: `build\maple tests\errors\runtime_stack_trace.ms 2>&1` → stack trace with line numbers

**REFACTOR**: Limit stack trace depth for deep recursion.

### Cycle 5: Import Error Messages with Searched Paths

**RED**: Import errors generic, no paths or circular chain.

- `tests/errors/import_not_found.ms`: `import nonexistent` → must show module name + searched paths
- `tests/errors/circular_import_a.ms` → `import circular_import_b`, and vice versa → must show chain: `Circular import detected: 'a' → 'b' → 'a'`

**GREEN**:
- `src/module.c`:
  - Not found: `Error: Module 'foo' not found. Searched: ./foo.ms, /path1/foo.ms`
  - Circular: `Error: Circular import detected: 'a' → 'b' → 'a'`
  - Missing export: `Error: Module 'math' has no export 'sqrt'`
- `ms_module_resolve_path()`: accumulate attempted paths list
- `detectCircularDependency()`: build chain string from loading stack

**Verify GREEN**: `build\maple tests\errors\import_not_found.ms 2>&1` → shows searched paths

**REFACTOR**: Import errors use same colored format as other errors.

### Cycle 6: End-to-End Error Format Validation

**RED**: Inconsistent formatting across error types.

- Validate: syntax error (line, column, source, pointer, color); runtime error (message, trace, color); import error (paths/chain)
- Manual review of all error output

**GREEN**:
- All paths: `[color]error:[reset] line X, column Y: message`
- Messages actionable (what's wrong + fix hint)
- First error stops compilation (single-error behavior)
- Review all `fprintf(stderr, ...)` for consistency

**Verify GREEN**: All error scenarios produce well-formatted, colored, helpful output

**REFACTOR**: Unify through single `ms_error_print()` function.

## Acceptance Criteria

- [ ] Compile error: file, line, column, message, source line + pointer
- [ ] Runtime error: message + full stack trace
- [ ] Import errors: search paths attempted
- [ ] Circular import: dependency chain shown
- [ ] Colored text on terminals, disabled when piped
- [ ] First error stops compilation
- [ ] Messages clear and actionable

## Notes

- Error structs from DESIGN.md §4: `MsCompileError`, `MsRuntimeError`
- Color: ANSI escapes, disabled when stderr not a terminal
- Stack trace: walk `MsCallFrame` array current→main
- Source line display requires storing line text in parser/compiler during parsing
- `^` pointer column must account for multi-byte chars or tabs
