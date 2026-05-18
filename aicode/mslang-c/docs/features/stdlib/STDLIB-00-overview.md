# STDLIB-00: 内置标准库总体设计

## 目标

为 mslang-c 提供 10 个内置标准库模块，通过 CAPI-01 注册表懒加载，对外公开 `MsModuleApi`（CAPI-05）供第三方扩展复用相同机制。

---

## 架构层次

```
.ms 用户代码
    import io; io.open(...)
        ↓
ms_module_load（src/module.c）
    ├─ module_cache 命中 → 返回
    ├─ builtin_registry 命中 → init(vm, mod) → 填充 exports
    ├─ 文件系统 .ms
    └─ 动态库 .so/.dll
        ↓
MsObjModule { exports: MsTable }
    exports["open"] = ObjNative(ms_io_open, arity=2)
    exports["stdin"] = ObjFile(stdout_fp)
```

---

## 模块注册入口（`include/ms/stdlib.h`，`src/vm.c`）

```c
void ms_stdlib_register_all(MsVM* vm);
```

在 `ms_vm_init` 末尾调用，只写注册表，不执行任何初始化逻辑。

```c
// src/stdlib/ 各模块的 init 声明：
void ms_module_math_init  (MsVM* vm, MsObjModule* mod);
void ms_module_os_init    (MsVM* vm, MsObjModule* mod);
void ms_module_time_init  (MsVM* vm, MsObjModule* mod);
void ms_module_io_init    (MsVM* vm, MsObjModule* mod);
void ms_module_buffer_init(MsVM* vm, MsObjModule* mod);
void ms_module_hash_init  (MsVM* vm, MsObjModule* mod);
void ms_module_log_init   (MsVM* vm, MsObjModule* mod);
void ms_module_net_init   (MsVM* vm, MsObjModule* mod);
void ms_module_debug_init (MsVM* vm, MsObjModule* mod);
void ms_module_gc_init    (MsVM* vm, MsObjModule* mod);
```

---

## 模块依赖图

```
buffer ──────────────────────────────────┐
                                         ▼
math, os, time ─────────────────────► (独立)
                                         │
io ──────────── 依赖 buffer, ObjFile ────┤
                依赖 threadpool (异步)   │
                                         ▼
net ──────────── 依赖 buffer, ObjSocket ─┤（复用 ASYNC-06 reactor）
                                         │
hash ─────────── 依赖 buffer ────────────┤
log ──────────── 依赖 io（File sink）────┤
debug ─────────── 依赖 vm internals ─────┤
gc ──────────── 依赖 vm_gc.h ────────────┘
```

---

## 文件布局

```
src/stdlib/
    math.c
    os.c
    time.c
    io.c       # 包含 ObjFile 实现
    buffer.c   # 包含 ObjBuffer 实现
    hash.c     # 包含 MsObjUserdata 用法（Hasher）
    log.c
    net.c      # tcp_* 迁移自 vm_natives.c
    debug.c
    gc.c
include/ms/
    stdlib.h              # ms_stdlib_register_all + 所有 init 声明
    stdlib/
        objfile.h         # MsObjFile 定义
        objbuffer.h       # MsObjBuffer 定义
```

---

## 实施顺序

> **注**：标签编号（STDLIB-01..10）表示功能序号，不等于实施顺序。推荐按依赖 DAG 构建：buffer → math → os → time → io → log → hash → net → debug → gc。

---

## 模块一览

| 模块 | 源文件 | 句柄类型 | 异步 |
|---|---|---|---|
| math | `stdlib/math.c` | 无 | 无 |
| os | `stdlib/os.c` | 无 | 无 |
| time | `stdlib/time.c` | 无 | `sleep` 返回 Future |
| io | `stdlib/io.c` | `ObjFile` | `read_file_async`/`write_file_async` |
| buffer | `stdlib/buffer.c` | `ObjBuffer` | 无 |
| hash | `stdlib/hash.c` | `MsObjUserdata`（Hasher）| 无 |
| log | `stdlib/log.c` | `MsObjUserdata`（Logger）| 无 |
| net | `stdlib/net.c` | `ObjSocket`（已有）| connect/accept/read/write 返回 Future |
| debug | `stdlib/debug.c` | 无 | 无 |
| gc | `stdlib/gc.c` | 无 | 无 |

> **注**：
> - hash：依赖 CAPI-06 §userdata 方法分发（v1 引入）
> - log：依赖 CAPI-06 §userdata 方法分发（v1 引入）

---

## CMakeLists.txt 变更

```cmake
# src/CMakeLists.txt 或根 CMakeLists.txt
target_sources(mslang-c PRIVATE
    src/module.c
    src/module_loader.c
    src/module_api.c
    src/dynlib.c
    src/threadpool.c
    src/stdlib/math.c
    src/stdlib/os.c
    src/stdlib/time.c
    src/stdlib/io.c
    src/stdlib/buffer.c
    src/stdlib/hash.c
    src/stdlib/log.c
    src/stdlib/net.c
    src/stdlib/debug.c
    src/stdlib/gc.c
)

# Unix 线程池
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(mslang-c PRIVATE Threads::Threads)
endif()

# Unix 动态加载
if(UNIX)
    target_link_libraries(mslang-c PRIVATE dl)
endif()
```

---

## 测试约定

每个模块一个单元测试文件：

```
tests/unit/
    test_stdlib_math.c
    test_stdlib_os.c
    ...
    test_stdlib_gc.c
    test_capi_dynamic.c

tests/fixtures/
    stdlib_math_basic.ms
    stdlib_os_basic.ms
    ...
    stdlib_ext/
        sample_ext.c
        CMakeLists.txt
```

单元测试模板：

```c
// tests/unit/test_stdlib_math.c
#include "ms/vm.h"
#include "tests/test_assert.h"

static void test_math_sqrt(void) {
    MsVM vm; ms_vm_init(&vm);
    const char* src = "import math; print(math.sqrt(4))";
    MsInterpretResult r = ms_vm_interpret(&vm, src, "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_math_sqrt();
    printf("test_stdlib_math: all passed\n");
    return 0;
}
```
