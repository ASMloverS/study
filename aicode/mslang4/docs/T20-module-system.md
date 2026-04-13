# T20: Module System

**Phase**: 11 · **Deps**: T15 (VM Core), T04 (Platform Layer), T17 (Garbage Collection) · **Complexity**: High

## Goal

Module system: `import module`, `from module import item (as alias)`. Modules loaded once (cached), isolated scope, circular dependency detection.

## Files

| File | Action |
|------|--------|
| `src/module.h` | Create: module loading API |
| `src/module.c` | Create: module loading impl |
| `src/compiler.c` | Modify: `compileImportStmt`, `OP_IMPORT`/`OP_IMPORT_FROM` |
| `src/vm.c` | Modify: `OP_IMPORT`, `OP_IMPORT_FROM` dispatch, module loading |
| `src/vm.h` | Modify: `ms_vm_import_module`, `ms_vm_import_from`, `ms_vm_add_module_path` |

## TDD Cycles

### Cycle 1: Module Struct & Cache Infrastructure

**RED**: `src/module.h`/`src/module.c` don't exist, module functions undefined → link error.

- `test_module_cache_empty`: `ms_module_get_loaded(vm, "math")` → NULL
- `test_module_cache_store`: store module in `vm->modules` → retrieve → same pointer
- `test_module_search_path`: `ms_module_add_search_path(vm, "/custom/path")` → path appended

**GREEN**:
- Create `src/module.h`:
  ```c
  #ifndef MS_MODULE_H
  #define MS_MODULE_H

  #include "vm.h"

  typedef struct {
      char* moduleName;
      char* message;
      int line;
  } MsModuleError;

  MsResult ms_module_load(MsVM* vm, const char* moduleName, MsModule** outModule);
  MsModule* ms_module_get_loaded(MsVM* vm, const char* moduleName);
  void ms_module_add_search_path(MsVM* vm, const char* path);
  char* ms_module_resolve_path(MsVM* vm, const char* moduleName);

  #endif
  ```
- Create `src/module.c`:
  - `ms_module_get_loaded()`: lookup `vm->modules` table by name → return or NULL
  - `ms_module_add_search_path()`: append to `vm->modulePaths` via `ms_reallocate()`
- `src/vm.h`: add `MsTable modules`, `char** modulePaths` / `int modulePathCount` / `int modulePathCapacity`, `MsTable loadingStack` to MsVM
- `src/vm.c`: `ms_vm_init()` → init modules table + modulePaths; `ms_vm_free()` → free paths + modules table

**Verify GREEN**: `cmake --build build && build\test_module`

**REFACTOR**: modulePaths uses standard growth pattern.

### Cycle 2: Path Resolution

**RED**: `ms_module_resolve_path` stub returns NULL.

- `test_resolve_path_current_dir`: temp `math.ms` in CWD → resolve("math") → path ending `math.ms`
- `test_resolve_path_search_paths`: custom search path + `utils.ms` → found
- `test_resolve_path_not_found`: resolve("nonexistent") → NULL

**GREEN**: `ms_module_resolve_path()`:
1. Try `./<name>.ms` via `ms_platform_file_exists()`
2. Try each `vm->modulePaths` entry: `<path>/<name>.ms`
3. Return allocated path or NULL

**Verify GREEN**: `cmake --build build && build\test_module`

**REFACTOR**: Use `ms_platform_read_file()` or raw `fopen` for existence check.

### Cycle 3: Module Load & Execute

**RED**: `ms_module_load` not fully implemented.

- `tests/modules/math.ms`: `var pi = 3.14159`
- `test_module_load_basic`: `ms_module_load(vm, "math", &mod)` → `MS_RESULT_OK`, mod non-NULL
- `test_module_load_caches`: load twice → same pointer (cached)

**GREEN**: `ms_module_load()`:
1. Check cache → return if found
2. Resolve path → `ms_module_resolve_path()`
3. Read file → `ms_platform_read_file()`
4. Compile → `ms_compiler_compile()`
5. Execute module body (new call frame, fresh globals scope)
6. Capture top-level declarations as exports
7. Cache in `vm->modules`
8. Return module
- Define MsModule struct: name, exports table, compiled chunk

**Verify GREEN**: `cmake --build build && build\test_module`

**REFACTOR**: Module execution errors propagated, no partial state in cache.

### Cycle 4: Circular Dependency Detection

**RED**: No circular check → hangs or stack overflow.

- `tests/modules/cycle_a.ms`: `import cycle_b`
- `tests/modules/cycle_b.ms`: `import cycle_a`
- `test_circular_dependency`: load `cycle_a` → `MS_RESULT_RUNTIME_ERROR`, message contains "Circular import detected"

**GREEN**:
- Internal `loadingStack` tracking in `vm->loadingStack`
- `ms_module_load()`: before compile → add name to loading stack; after load → remove; if name already in stack → error: `Circular import detected: 'a' → 'b' → 'a'`

**Verify GREEN**: `cmake --build build && build\test_module`

**REFACTOR**: Format chain showing full import path.

### Cycle 5: Compiler — `import module` Syntax

**RED**: `import` keyword not recognized → compile error.

- `tests/modules/greet.ms`: `var greeting = "hello"`
- `tests/integration/test_import.ms`: `import greet` → runs without error

**GREEN**:
- `src/compiler.c`: add `OP_IMPORT`. `compileImportStmt()`: consume `import` + identifier → emit `OP_IMPORT(moduleName)`
- `src/vm.c`: `OP_IMPORT` → read name → `ms_module_load()` → push module value
- Add `import` to scanner keyword table

**Verify GREEN**: `build\maple tests\integration\test_import.ms` → no error

**REFACTOR**: Decide: push module object or just load as side effect.

### Cycle 6: Compiler — `from module import item as alias` Syntax

**RED**: `from` keyword not recognized → compile error.

- `tests/modules/mathlib.ms`: `var sqrt = 42\nvar pi = 3.14`
- `tests/integration/test_from_import.ms`: `from mathlib import sqrt\nprint sqrt` → "42"
- `tests/integration/test_from_import_alias.ms`: `from mathlib import sqrt as square_root\nprint square_root` → "42"

**GREEN**:
- `src/compiler.c`: add `OP_IMPORT_FROM`. `compileImportStmt()` handles `from`:
  - `from module import item`: consume `from` + module name + `import` + item → emit `OP_IMPORT_FROM(module, item)`
  - `from module import item as alias`: additionally read `as` + alias → bind under alias
- `src/vm.c`: `OP_IMPORT_FROM` → load module → lookup item in exports → push value → bind to name/alias
- Add `from` and `as` to scanner keywords

**Verify GREEN**: `build\maple tests\integration\test_from_import.ms` → "42"

**REFACTOR**: Consolidate import parsing into single function with branching.

### Cycle 7: Import Error Messages

**RED**: Error messages generic, missing search paths/export names.

- `test_import_missing.ms`: `import nonexistent_module` → error with searched paths
- `test_import_missing_export.ms`: `from mathlib import not_a_thing` → error naming missing export

**GREEN**:
- `ms_module_resolve_path()` error: include all searched paths → `Module 'foo' not found. Searched: ./foo.ms, /path1/foo.ms`
- `OP_IMPORT_FROM` dispatch: `Module 'math' has no export 'sqrt'`

**Verify GREEN**: `build\maple tests\integration\test_import_missing.ms` → detailed error with paths

**REFACTOR**: Error messages consistent with compile/runtime error format from T23.

## Acceptance Criteria

- [ ] `import math` loads math.ms and caches
- [ ] `from math import sqrt` imports specific symbol
- [ ] `from math import sqrt as square_root` works
- [ ] Importing same module twice → loads once
- [ ] Circular: A→B→A → clear error
- [ ] Module not found → error with searched paths
- [ ] Module exports accessible from importer
- [ ] Custom search paths via `ms_vm_add_module_path()`

## Notes

- Modules use `ms_platform_read_file()` for file I/O
- Loading stack for circular detection: `vm->loadingStack`
- Exports captured by recording top-level variable declarations after execution
- Import errors include all searched paths for debugging
