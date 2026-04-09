# Task 26: Module System

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement import/from-import/aliased-import, ObjModule with export table, module caching, circular dependency detection.
**Dependencies:** T13, T18
**Produces:** `import "path"`, `from "path" import name`, `from "path" import name as alias`

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/module.h` | MsModuleLoader API |
| Create | `src/module.c` | Path resolution, file reading |
| Modify | `include/ms/object.h` | ObjModule struct |
| Modify | `src/object.c` | Module create/destroy |
| Create | `src/vm_import.c` | IMPORT/IMPFROM/IMPALIAS implementation |
| Modify | `src/compiler.c` | Import statement compilation |
| Modify | `src/vm.c` | Import opcode dispatch |
| Create | `tests/unit/test_modules.c` | Module tests |

## Key Data Structures / API

```c
// ObjModule
typedef enum {
    MS_MOD_UNSEEN,
    MS_MOD_INITIALIZING,
    MS_MOD_INITIALIZED,
    MS_MOD_FAILED,
} MsModuleState;

typedef struct {
    MsObject obj;
    MsObjString* name;
    MsObjString* path;      // canonical file path
    MsTable exports;         // exported bindings
    MsModuleState state;
} MsObjModule;

#define MS_IS_MODULE(v)  MS_IS_OBJ_TYPE(v, MS_OBJ_MODULE)
#define MS_AS_MODULE(v)  ((MsObjModule*)MS_AS_OBJECT(v))

MsObjModule* ms_obj_module_new(MsVM* vm, MsObjString* name, MsObjString* path);
```

```c
// include/ms/module.h
#pragma once
#include "ms/vm.h"

// Module cache (by canonical path)
// Append to MsVM:
//   MsTable module_cache;  // path → ObjModule

// Load and execute a module; returns ObjModule* (cache hit returns immediately)
MsObjModule* ms_module_load(MsVM* vm, const char* import_path,
                             const char* from_path);
// Read file contents (heap-allocated, caller frees)
char* ms_read_file(const char* path);
// Resolve relative path → absolute path
char* ms_resolve_path(const char* import_path, const char* from_dir);
```

## Implementation Notes

### Import Syntax

```ms
import "math"              // → ObjModule as namespace
from "math" import sqrt    // → import a single binding
from "math" import sqrt as s  // → aliased
```

### Compilation

- `import "path"` → `IMPORT A Bx` (K(Bx) = path string, R(A) = ObjModule)
- `from "path" import name` → `IMPORT A Bx` + `IMPFROM A A C` (K(C) = name)
- `from "path" import name as alias` → same as above + `IMPALIAS`

### VM Execution

```c
case MS_OP_IMPORT: {
    MsObjString* path = MS_AS_STRING(K(MS_GET_Bx(instr)));
    MsObjModule* mod = ms_module_load(vm, path->data,
                                       frame->closure->function->script_path);
    if (!mod) { runtime_error("Failed to import '%s'", path->data); break; }
    R(A) = MS_OBJ_VAL(mod);
    break;
}
case MS_OP_IMPFROM: {
    MsObjModule* mod = MS_AS_MODULE(R(MS_GET_B(instr)));
    MsObjString* name = MS_AS_STRING(K(MS_GET_C(instr)));
    MsValue val;
    if (!ms_table_get(&mod->exports, name, &val)) {
        runtime_error("Module has no export '%s'", name->data);
        break;
    }
    R(A) = val;
    break;
}
```

### Module Load Flow

```c
MsObjModule* ms_module_load(MsVM* vm, const char* import_path, const char* from) {
    char* resolved = ms_resolve_path(import_path, from);
    MsObjString* key = ms_obj_string_copy(vm, resolved, strlen(resolved));
    // 1. Cache hit?
    MsValue cached;
    if (ms_table_get(&vm->module_cache, key, &cached))
        return MS_AS_MODULE(cached);
    // 2. Circular dependency detection
    MsObjModule* mod = ms_obj_module_new(vm, ...);
    mod->state = MS_MOD_INITIALIZING;
    ms_table_set(&vm->module_cache, key, MS_OBJ_VAL(mod));
    // 3. Read and compile
    char* source = ms_read_file(resolved);
    MsObjFunction* fn = ms_compile(vm, source, resolved, ...);
    free(source);
    // 4. Execute module top-level code
    ms_vm_execute_module(vm, fn, mod);
    mod->state = MS_MOD_INITIALIZED;
    return mod;
}
```

### Module Exports

Top-level `var`/`fun`/`class` declarations automatically become exports. Implementation: after execution, copy marked bindings from globals into `mod->exports`.

Simpler alternative: during module execution, `DEFGLOBAL` writes to `mod->exports` instead of `vm->globals`.

### Path Resolution

- Relative paths: relative to the importing file's directory
- Automatically append `.ms` extension
- Normalize to absolute path for cache key

## C Unit Tests

```c
// tests/unit/test_modules.c
#include "test_assert.h"
#include "ms/module.h"

static void test_read_file(void) {
    // Create a temp file for testing
    FILE* f = fopen("_test_mod.ms", "w");
    fprintf(f, "var x = 42\n");
    fclose(f);
    char* src = ms_read_file("_test_mod.ms");
    TEST_ASSERT(src != NULL);
    TEST_ASSERT(strstr(src, "42") != NULL);
    free(src);
    remove("_test_mod.ms");
}

int main(void) {
    test_read_file();
    printf("test_modules: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/modules/math_mod.ms
var PI = 3.14159
fun square(x) { return x * x }
fun cube(x) { return x * x * x }

// tests/fixtures/import_basic.ms
import "modules/math_mod"
print(math_mod.PI)
// expect: 3.14159
print(math_mod.square(4))
// expect: 16

// tests/fixtures/from_import.ms
from "modules/math_mod" import square
print(square(5))
// expect: 25

// tests/fixtures/import_alias.ms
from "modules/math_mod" import square as sq
print(sq(6))
// expect: 36

// tests/fixtures/modules/greeter.ms
fun hello(name) {
  return "Hello, " + name + "!"
}

// tests/fixtures/import_multiple.ms
from "modules/math_mod" import PI
from "modules/greeter" import hello
print(PI)
// expect: 3.14159
print(hello("World"))
// expect: Hello, World!
```
