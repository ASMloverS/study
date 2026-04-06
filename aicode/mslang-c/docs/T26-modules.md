# Task 26: Module System

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement import/from-import/aliased-import, ObjModule with export table, module caching, circular dependency detection.
**Dependencies:** T13, T18
**Produces:** `import "path"`, `from "path" import name`, `from "path" import name as alias`

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/module.h` | MsModuleLoader API |
| Create | `src/module.c` | 路径解析, 文件读取 |
| Modify | `include/ms/object.h` | ObjModule 结构 |
| Modify | `src/object.c` | 模块创建/销毁 |
| Create | `src/vm_import.c` | IMPORT/IMPFROM/IMPALIAS 实现 |
| Modify | `src/compiler.c` | import 语句编译 |
| Modify | `src/vm.c` | import 操作码分派 |
| Create | `tests/unit/test_modules.c` | 模块测试 |

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
    MsTable exports;         // 导出的绑定
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

// 模块缓存 (by canonical path)
// 追加到 MsVM:
//   MsTable module_cache;  // path → ObjModule

// 加载并执行模块; 返回 ObjModule* (缓存命中直接返回)
MsObjModule* ms_module_load(MsVM* vm, const char* import_path,
                             const char* from_path);
// 读取文件内容 (heap alloc, caller frees)
char* ms_read_file(const char* path);
// 解析相对路径 → 绝对路径
char* ms_resolve_path(const char* import_path, const char* from_dir);
```

## Implementation Notes

### Import 语法

```ms
import "math"              // → ObjModule as namespace
from "math" import sqrt    // → 导入单个绑定
from "math" import sqrt as s  // → 别名
```

### 编译

- `import "path"` → `IMPORT A Bx` (K(Bx) = 路径字符串, R(A) = ObjModule)
- `from "path" import name` → `IMPORT A Bx` + `IMPFROM A A C` (K(C) = name)
- `from "path" import name as alias` → 同上 + `IMPALIAS`

### VM 执行

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

### 模块加载流程

```c
MsObjModule* ms_module_load(MsVM* vm, const char* import_path, const char* from) {
    char* resolved = ms_resolve_path(import_path, from);
    MsObjString* key = ms_obj_string_copy(vm, resolved, strlen(resolved));
    // 1. 缓存命中?
    MsValue cached;
    if (ms_table_get(&vm->module_cache, key, &cached))
        return MS_AS_MODULE(cached);
    // 2. 循环依赖检测
    MsObjModule* mod = ms_obj_module_new(vm, ...);
    mod->state = MS_MOD_INITIALIZING;
    ms_table_set(&vm->module_cache, key, MS_OBJ_VAL(mod));
    // 3. 读取并编译
    char* source = ms_read_file(resolved);
    MsObjFunction* fn = ms_compile(vm, source, resolved, ...);
    free(source);
    // 4. 执行模块顶层代码
    ms_vm_execute_module(vm, fn, mod);
    mod->state = MS_MOD_INITIALIZED;
    return mod;
}
```

### 模块导出

模块顶层的 `var`/`fun`/`class` 声明自动成为导出. 实现: 模块执行后, 将 globals 中以模块内部前缀标记的绑定复制到 `mod->exports`.

或更简单: 模块执行时, DEFGLOBAL 写入 mod->exports 而非 vm->globals.

### 路径解析

- 相对路径: 相对于导入者所在目录
- 自动添加 `.ms` 后缀
- 规范化为绝对路径用于缓存 key

## C Unit Tests

```c
// tests/unit/test_modules.c
#include "test_assert.h"
#include "ms/module.h"

static void test_read_file(void) {
    // 创建临时文件测试
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
