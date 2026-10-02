# T001 项目骨架 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立可三平台构建的空壳工程：CMake 顶层、common 基础设施、plat 平台薄封装、ms 主命令（仅 version）、CI 工作流与格式化配置。

**Architecture:** 纯 C11 单仓；`src/` 按架构文档 §4 分模块占位（12 个空编译单元 foreach 建静态库），`tools/` 链接全部模块产出唯一可执行 `ms`；构建输出仅 `build/`。规格来源：`docs/tasks/T001-project-skeleton.md`。

**Tech Stack:** C11（禁扩展）/ CMake ≥3.16（presets 需 ≥3.21）/ MSVC 2022、GCC、Clang / ctest / clang-format 18。

**约定：** 所有命令在 `aicode/mslang-cc/` 目录执行（PowerShell 5.1）；文本文件一律 UTF-8 无 BOM + LF；C 代码风格遵循 `docs/language/28-style-c.md`（写完每个 .c/.h 后立即 `clang-format -i`）。

---

### Task 1: 构建骨架、占位模块与 ms version

**Files:**
- Create: `CMakeLists.txt`、`CMakePresets.json`、`cmake/CompilerWarnings.cmake`
- Create: `src/<mod>/<mod>.c`（mod = scanner/parser/binder/compiler/vm/obj/gc/sched/reactor/module/ms_api/stdlib，共 12 个占位空编译单元）
- Create: `tools/ms.c`、`.clang-format`、`.editorconfig`、`.gitignore`

- [ ] **Step 1: 写顶层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

project(mslang C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(cmake/CompilerWarnings.cmake)

# 探测 computed goto：GCC/Clang 可用则定义，MSVC 因 -std=c11 无法解析而恒为否（见 T011 dispatch 双路径）
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-std=c11")
check_c_source_compiles("
int main(void) {
  static void* table[] = {&&label};
  goto* table[0];
label:
  return 0;
}
" MS_HAVE_COMPUTED_GOTO)
unset(CMAKE_REQUIRED_FLAGS)
if(MS_HAVE_COMPUTED_GOTO)
  add_compile_definitions(MS_USE_COMPUTED_GOTO=1)
endif()

option(MS_SANITIZERS "Enable ASan+UBSan (GCC/Clang only)" OFF)
if(MS_SANITIZERS AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(ms_common STATIC src/common/common.c)
target_include_directories(ms_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(ms_plat STATIC src/plat/plat_thread.c src/plat/plat_time.c)
target_include_directories(ms_plat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ms_plat PUBLIC ms_common)

# 语言模块占位：空编译单元建库，后续任务按目录向库内追加源文件（见 docs/language/20-architecture.md §4）
foreach(mod IN ITEMS scanner parser binder compiler vm obj gc sched reactor module ms_api stdlib)
  add_library(ms_${mod} STATIC src/${mod}/${mod}.c)
  target_include_directories(ms_${mod} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
endforeach()

add_executable(ms tools/ms.c)
target_link_libraries(
  ms PRIVATE ms_common ms_plat ms_scanner ms_parser ms_binder ms_compiler ms_vm ms_obj ms_gc ms_sched
  ms_reactor ms_module ms_ms_api ms_stdlib
)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2: 写 cmake/CompilerWarnings.cmake**

```cmake
# 警告门禁：MSVC /W4 /WX；GCC/Clang -Wall -Wextra -Werror -Wshadow
# /utf-8：MSVC 源码按 UTF-8 解析（无 BOM 中文注释防 C4819）；/experimental:c11atomics：<stdatomic.h> 前置开关
if(MSVC)
  add_compile_options(/W4 /WX /utf-8 /experimental:c11atomics)
else()
  add_compile_options(-Wall -Wextra -Werror -Wshadow)
  if(NOT WIN32)
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
    add_link_options(-pthread)
  endif()
endif()
```

- [ ] **Step 3: 写 CMakePresets.json**（四个预设共用唯一构建目录 `build/`；切换生成器需先删 `build/`）

```json
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "configurePresets": [
    {
      "name": "msvc",
      "displayName": "MSVC VS2022 (multi-config)",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build",
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" }
    },
    {
      "name": "debug",
      "displayName": "Ninja Debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "release",
      "displayName": "Ninja Release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    },
    {
      "name": "asan",
      "displayName": "Ninja ASan+UBSan (GCC/Clang, CI 用)",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "MS_SANITIZERS": "ON" },
      "condition": { "type": "notEquals", "lhs": "${hostSystemName}", "rhs": "Windows" }
    }
  ]
}
```

- [ ] **Step 4: 建 tests/CMakeLists.txt 占位**（Task 2 起填测试目标；空文件会让 add_subdirectory 报错）

```cmake
# 测试目标由后续任务填入（T002 引入单测框架）
```

- [ ] **Step 5: 生成 12 个占位空编译单元**（UTF-8 无 BOM + LF，WriteAllText 显式控制）

```powershell
$enc = New-Object System.Text.UTF8Encoding($false)
foreach ($m in @('scanner','parser','binder','compiler','vm','obj','gc','sched','reactor','module','ms_api','stdlib')) {
  New-Item -ItemType Directory -Force "src/$m" | Out-Null
  [IO.File]::WriteAllText("$PWD/src/$m/$m.c", "// 占位编译单元：本目录内容由后续任务按 docs/tasks/README.md 填入`n", $enc)
}
```

- [ ] **Step 6: 写 tools/ms.c**

```c
#include <stdio.h>
#include <string.h>

#include "common/common.h"

static void printUsage(const char* program) {
  fprintf(stderr, "usage: %s <command> [args]\n", program);
  fprintf(stderr, "commands:\n");
  fprintf(stderr, "  version    print version\n");
}

int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "version") == 0) {
    printf("mslang %s\n", MS_VERSION);
    return 0;
  }
  printUsage(argv[0]);
  return 1;
}
```

注：本步尚无 `common.h`，编译会在 Task 2 前失败是预期的——Step 8 的构建验证放在临时注释掉 include 与 MS_VERSION 之前？不：**本任务内先建 `src/common/common.h` 最小版**（仅 `MS_VERSION` 宏），Task 2 再扩充定宽别名/FNV。`src/common/common.c` 同步建空实现文件（仅含 `#include "common.h"`），保证 Task 1 库可编译。

`src/common/common.h`（Task 1 最小版）：

```c
#ifndef INCLUDE_MS_COMMON_COMMON_H_
#define INCLUDE_MS_COMMON_COMMON_H_

#define MS_VERSION "0.0.1 (dev)"

#endif
```

`src/common/common.c`（Task 1 最小版）：

```c
#include "common.h"
```

- [ ] **Step 7: 写 .clang-format / .editorconfig / .gitignore**

`.clang-format`：

```yaml
BasedOnStyle: Google
IndentWidth: 2
ColumnLimit: 120
PointerAlignment: Right
DerivePointerAlignment: false
SortIncludes: false
IncludeBlocks: Preserve
```

`.editorconfig`：

```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true

[*.{c,h}]
indent_style = space
indent_size = 2
```

`.gitignore`：

```text
build/
__mscache__/
ms_packages/
```

- [ ] **Step 8: 配置 + 双配置构建验证**

Run: `clang-format -i tools/ms.c src/common/common.c src/common/common.h`
Run: `cmake --preset msvc`
Expected: `-- Selecting Windows SDK ... -- Configuring done -- Generating done`

Run: `cmake --build build --config Debug`
Expected: `ms.vcxproj -> ...build\Debug\ms.exe`，0 error 0 warning

Run: `cmake --build build --config Release`
Expected: `ms.vcxproj -> ...build\Release\ms.exe`

Run: `& .\build\Debug\ms.exe version; $LASTEXITCODE`
Expected: 输出 `mslang 0.0.1 (dev)`，退出码 `0`

Run: `& .\build\Debug\ms.exe; $LASTEXITCODE`
Expected: usage 输出到 stderr，退出码 `1`

Run: `git status --short`
Expected: `build/` 不出现（被忽略），无其他位置产物

- [ ] **Step 9: Commit**

```powershell
git add CMakeLists.txt CMakePresets.json cmake/ tests/CMakeLists.txt src/ tools/ .clang-format .editorconfig .gitignore
git commit -m "🎉 feat(build): scaffold CMake build with platform placeholders and ms version"
```

---

### Task 2: common 定宽别名、数组宏与 FNV-1a（红→绿）

**Files:**
- Modify: `src/common/common.h`（扩充）、`src/common/common.c`（实现 FNV）
- Create: `tests/unit/test_common.c`
- Modify: `tests/CMakeLists.txt`（注册测试目标）

- [ ] **Step 1: 写失败测试 tests/unit/test_common.c**

```c
#include <stdio.h>
#include <stdlib.h>

#include "common/common.h"

_Static_assert(sizeof(ms_u8) == 1, "ms_u8 must be 8 bits");
_Static_assert(sizeof(ms_i32) == 4, "ms_i32 must be 32 bits");
_Static_assert(sizeof(ms_u32) == 4, "ms_u32 must be 32 bits");
_Static_assert(sizeof(ms_i64) == 8, "ms_i64 must be 64 bits");
_Static_assert(sizeof(ms_f64) == 8, "ms_f64 must be 64 bits");

static int gFailures = 0;

#define CHECK(cond)                                                                             \
  do {                                                                                          \
    if (!(cond)) {                                                                              \
      gFailures++;                                                                              \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                           \
    }                                                                                           \
  } while (0)

static void testFnv1a32(void) {
  CHECK(msFnv1a32("", 0) == 0x811c9dc5u);
  CHECK(msFnv1a32("a", 1) == 0xe40c292cu);
  CHECK(msFnv1a32("foobar", 6) == 0xbf9cf968u);
}

static void testFnv1a64(void) {
  CHECK(msFnv1a64("", 0) == 0xcbf29ce484222325ULL);
  CHECK(msFnv1a64("a", 1) == 0xaf63dc4c8601ec8cULL);
  CHECK(msFnv1a64("foobar", 6) == 0x85944171f73967e8ULL);
}

static void testArrayPush(void) {
  int* arr = NULL;
  int count = 0;
  int cap = 0;
  for (int i = 0; i < 100; i++) {
    MS_ARRAY_PUSH(arr, count, cap, i * 3);
  }
  CHECK(count == 100);
  CHECK(cap >= 100);
  bool values = true;
  for (int i = 0; i < 100; i++) {
    if (arr[i] != i * 3) { values = false; }
  }
  CHECK(values);
  int oldCap = cap;
  MS_ARRAY_PUSH(arr, count, cap, 42);
  CHECK(cap >= oldCap);
  free(arr);
}

int main(void) {
  testFnv1a32();
  testFnv1a64();
  testArrayPush();
  if (gFailures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", gFailures);
  return 1;
}
```

- [ ] **Step 2: tests/CMakeLists.txt 注册目标**

```cmake
add_executable(test_common unit/test_common.c)
target_link_libraries(test_common PRIVATE ms_common ms_plat)
add_test(NAME common COMMAND test_common)
```

- [ ] **Step 3: 跑测试确认失败（红）**

Run: `cmake --build build --config Debug`
Expected: 编译失败——`common/common.h` 无 `ms_u8`/`msFnv1a32` 等定义（MSVC: `error C2065`/`error C3861` 未声明的标识符）

- [ ] **Step 4: 扩充 src/common/common.h 为完整版**

```c
#ifndef INCLUDE_MS_COMMON_COMMON_H_
#define INCLUDE_MS_COMMON_COMMON_H_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t ms_u8;
typedef int32_t ms_i32;
typedef uint32_t ms_u32;
typedef int64_t ms_i64;
typedef uint64_t ms_u64;
typedef double ms_f64;

#define MS_VERSION "0.0.1 (dev)"

// 仅内部不变量断言：Release（NDEBUG）编译剔除
#if defined(NDEBUG)
#define MS_ASSERT(cond) ((void)0)
#else
#define MS_ASSERT(cond) assert(cond)
#endif

// 统一动态数组增长：容量从 0 起按 2 倍扩张，arr/count/cap 均为左值
#define MS_ARRAY_PUSH(arr, count, cap, elem)                                                    \
  do {                                                                                          \
    if ((count) == (cap)) {                                                                     \
      (cap) = (cap) == 0 ? 8 : (cap) * 2;                                                       \
      (arr) = realloc((arr), sizeof(*(arr)) * (size_t)(cap));                                   \
      MS_ASSERT((arr) != NULL);                                                                 \
    }                                                                                           \
    (arr)[(count)] = (elem);                                                                    \
    (count)++;                                                                                  \
  } while (0)

ms_u32 msFnv1a32(const void* data, ms_u64 len);
ms_u64 msFnv1a64(const void* data, ms_u64 len);

#endif
```

- [ ] **Step 5: 实现 src/common/common.c**

```c
#include "common.h"

ms_u32 msFnv1a32(const void* data, ms_u64 len) {
  const ms_u8* bytes = data;
  ms_u32 hash = 2166136261u;
  for (ms_u64 i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

ms_u64 msFnv1a64(const void* data, ms_u64 len) {
  const ms_u8* bytes = data;
  ms_u64 hash = 14695981039346656037ULL;
  for (ms_u64 i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}
```

- [ ] **Step 6: 跑测试确认通过（绿）**

Run: `clang-format -i src/common/common.h src/common/common.c tests/unit/test_common.c`
Run: `cmake --build build --config Debug`
Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: `100% tests passed, 1 tests failed` 之外——即 `1/1 test_common ... Passed`，`100% tests passed`

- [ ] **Step 7: Commit**

```powershell
git add src/common/ tests/unit/test_common.c tests/CMakeLists.txt
git commit -m "✨ feat(common): add fixed-width aliases, MS_ARRAY_PUSH and FNV-1a with tests"
```

---

### Task 3: plat 线程/原子/时间/IO 占位（红→绿）

**Files:**
- Create: `src/plat/plat_thread.h`、`src/plat/plat_thread.c`、`src/plat/plat_atomic.h`、`src/plat/plat_time.h`、`src/plat/plat_time.c`、`src/plat/plat_io.h`
- Modify: `tests/unit/test_common.c`（追加 atomic 与线程冒烟用例）

- [ ] **Step 1: 追加失败测试（test_common.c）**

在 `#include "common/common.h"` 后追加两个 include，在 `main` 前追加两个测试函数，并注册到 `main`：

```c
#include "plat/plat_atomic.h"
#include "plat/plat_thread.h"
```

```c
static void testAtomicSmoke(void) {
  PlatAtomic a;
  platAtomicInit(&a, 0);
  platAtomicStore(&a, 42, memory_order_relaxed);
  CHECK(platAtomicLoad(&a, memory_order_acquire) == 42);
  CHECK(platAtomicAdd(&a, 1, memory_order_relaxed) == 42);
  CHECK(platAtomicLoad(&a, memory_order_relaxed) == 43);
  CHECK(platAtomicCas(&a, 43, 100, memory_order_acq_rel));
  CHECK(platAtomicLoad(&a, memory_order_relaxed) == 100);
}

typedef struct CounterArgs {
  PlatMutex mutex;
  int value;
} CounterArgs;

static void incrementWorker(void* arg) {
  CounterArgs* counter = arg;
  for (int i = 0; i < 10000; i++) {
    platMutexLock(&counter->mutex);
    counter->value++;
    platMutexUnlock(&counter->mutex);
  }
}

static void testThreadSmoke(void) {
  CounterArgs counter;
  CHECK(platMutexInit(&counter.mutex));
  counter.value = 0;
  PlatThread* threads[4];
  for (int i = 0; i < 4; i++) {
    CHECK(platThreadCreate(&threads[i], incrementWorker, &counter));
  }
  for (int i = 0; i < 4; i++) {
    platThreadJoin(threads[i]);
    platThreadDestroy(threads[i]);
  }
  CHECK(counter.value == 40000);
  platMutexDestroy(&counter.mutex);
}
```

`main` 中 `testArrayPush();` 之后追加两行：`testAtomicSmoke();` 与 `testThreadSmoke();`

- [ ] **Step 2: 跑测试确认失败（红）**

Run: `cmake --build build --config Debug`
Expected: 编译失败——`plat/plat_atomic.h`/`plat/plat_thread.h` 不存在（MSVC: `error C1083` 无法打开包含文件）

- [ ] **Step 3: 写 src/plat/plat_thread.h**

```c
#ifndef INCLUDE_MS_PLAT_PLAT_THREAD_H_
#define INCLUDE_MS_PLAT_PLAT_THREAD_H_

#include <stdbool.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct PlatThread PlatThread;
typedef void (*PlatThreadFn)(void* arg);

bool platThreadCreate(PlatThread** t, PlatThreadFn fn, void* arg);
void platThreadJoin(PlatThread* t);
void platThreadDestroy(PlatThread* t);

typedef struct PlatMutex {
#if defined(_WIN32)
  SRWLOCK lock;
#else
  pthread_mutex_t mutex;
#endif
} PlatMutex;

bool platMutexInit(PlatMutex* m);
void platMutexDestroy(PlatMutex* m);
void platMutexLock(PlatMutex* m);
bool platMutexTryLock(PlatMutex* m);
void platMutexUnlock(PlatMutex* m);

typedef struct PlatCond {
#if defined(_WIN32)
  CONDITION_VARIABLE cond;
#else
  pthread_cond_t cond;
#endif
} PlatCond;

bool platCondInit(PlatCond* c);
void platCondDestroy(PlatCond* c);
void platCondWait(PlatCond* c, PlatMutex* m);
void platCondSignal(PlatCond* c);
void platCondBroadcast(PlatCond* c);

#endif
```

- [ ] **Step 4: 写 src/plat/plat_thread.c**

```c
#include "plat_thread.h"

#include <stdlib.h>

typedef struct ThreadStart {
  PlatThreadFn fn;
  void* arg;
} ThreadStart;

#if defined(_WIN32)

struct PlatThread {
  HANDLE handle;
};

static DWORD WINAPI threadTrampoline(LPVOID param) {
  ThreadStart start = *(ThreadStart*)param;
  free(param);
  start.fn(start.arg);
  return 0;
}

bool platThreadCreate(PlatThread** t, PlatThreadFn fn, void* arg) {
  *t = malloc(sizeof(PlatThread));
  if (*t == NULL) { return false; }
  ThreadStart* start = malloc(sizeof(ThreadStart));
  if (start == NULL) {
    free(*t);
    *t = NULL;
    return false;
  }
  start->fn = fn;
  start->arg = arg;
  (*t)->handle = CreateThread(NULL, 0, threadTrampoline, start, 0, NULL);
  if ((*t)->handle == NULL) {
    free(start);
    free(*t);
    *t = NULL;
    return false;
  }
  return true;
}

void platThreadJoin(PlatThread* t) { WaitForSingleObject(t->handle, INFINITE); }

void platThreadDestroy(PlatThread* t) {
  if (t == NULL) { return; }
  CloseHandle(t->handle);
  free(t);
}

bool platMutexInit(PlatMutex* m) {
  InitializeSRWLock(&m->lock);
  return true;
}

void platMutexDestroy(PlatMutex* m) { (void)m; }

void platMutexLock(PlatMutex* m) { AcquireSRWLockExclusive(&m->lock); }

bool platMutexTryLock(PlatMutex* m) { return TryAcquireSRWLockExclusive(&m->lock) != 0; }

void platMutexUnlock(PlatMutex* m) { ReleaseSRWLockExclusive(&m->lock); }

bool platCondInit(PlatCond* c) {
  InitializeConditionVariable(&c->cond);
  return true;
}

void platCondDestroy(PlatCond* c) { (void)c; }

void platCondWait(PlatCond* c, PlatMutex* m) {
  SleepConditionVariableSRW(&c->cond, &m->lock, INFINITE, 0);
}

void platCondSignal(PlatCond* c) { WakeConditionVariable(&c->cond); }

void platCondBroadcast(PlatCond* c) { WakeAllConditionVariable(&c->cond); }

#else

struct PlatThread {
  pthread_t thread;
};

static void* threadTrampoline(void* param) {
  ThreadStart start = *(ThreadStart*)param;
  free(param);
  start.fn(start.arg);
  return NULL;
}

bool platThreadCreate(PlatThread** t, PlatThreadFn fn, void* arg) {
  *t = malloc(sizeof(PlatThread));
  if (*t == NULL) { return false; }
  ThreadStart* start = malloc(sizeof(ThreadStart));
  if (start == NULL) {
    free(*t);
    *t = NULL;
    return false;
  }
  start->fn = fn;
  start->arg = arg;
  if (pthread_create(&(*t)->thread, NULL, threadTrampoline, start) != 0) {
    free(start);
    free(*t);
    *t = NULL;
    return false;
  }
  return true;
}

void platThreadJoin(PlatThread* t) { pthread_join(t->thread, NULL); }

void platThreadDestroy(PlatThread* t) {
  if (t == NULL) { return; }
  free(t);
}

bool platMutexInit(PlatMutex* m) { return pthread_mutex_init(&m->mutex, NULL) == 0; }

void platMutexDestroy(PlatMutex* m) { pthread_mutex_destroy(&m->mutex); }

void platMutexLock(PlatMutex* m) { pthread_mutex_lock(&m->mutex); }

bool platMutexTryLock(PlatMutex* m) { return pthread_mutex_trylock(&m->mutex) == 0; }

void platMutexUnlock(PlatMutex* m) { pthread_mutex_unlock(&m->mutex); }

bool platCondInit(PlatCond* c) { return pthread_cond_init(&c->cond, NULL) == 0; }

void platCondDestroy(PlatCond* c) { pthread_cond_destroy(&c->cond); }

void platCondWait(PlatCond* c, PlatMutex* m) { pthread_cond_wait(&c->cond, &m->mutex); }

void platCondSignal(PlatCond* c) { pthread_cond_signal(&c->cond); }

void platCondBroadcast(PlatCond* c) { pthread_cond_broadcast(&c->cond); }

#endif
```

- [ ] **Step 5: 写 src/plat/plat_atomic.h**（memory order 显式；`<stdatomic.h>` 薄封装）

```c
#ifndef INCLUDE_MS_PLAT_PLAT_ATOMIC_H_
#define INCLUDE_MS_PLAT_PLAT_ATOMIC_H_

#include <stdbool.h>
#include <stdatomic.h>

#include "common/common.h"

typedef struct PlatAtomic {
  _Atomic ms_i64 value;
} PlatAtomic;

static inline void platAtomicInit(PlatAtomic* a, ms_i64 value) { atomic_init(&a->value, value); }

static inline ms_i64 platAtomicLoad(const PlatAtomic* a, memory_order order) {
  return atomic_load_explicit(&a->value, order);
}

static inline void platAtomicStore(PlatAtomic* a, ms_i64 value, memory_order order) {
  atomic_store_explicit(&a->value, value, order);
}

static inline ms_i64 platAtomicAdd(PlatAtomic* a, ms_i64 delta, memory_order order) {
  return atomic_fetch_add_explicit(&a->value, delta, order);
}

static inline bool platAtomicCas(PlatAtomic* a, ms_i64 expected, ms_i64 desired, memory_order order) {
  return atomic_compare_exchange_strong_explicit(&a->value, &expected, desired, order, order);
}

#endif
```

- [ ] **Step 6: 写 src/plat/plat_time.h 与 plat_time.c**

`src/plat/plat_time.h`：

```c
#ifndef INCLUDE_MS_PLAT_PLAT_TIME_H_
#define INCLUDE_MS_PLAT_PLAT_TIME_H_

#include "common/common.h"

ms_i64 platTimeMonotonicNs(void);
ms_i64 platTimeMonotonicMs(void);
ms_i64 platTimeWallNs(void);
ms_i64 platTimeWallMs(void);

#endif
```

`src/plat/plat_time.c`：

```c
#include "plat_time.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// QPC 频率恒定；先除后乘避免 counter * 1e9 溢出 int64
static ms_i64 qpcToNs(LONGLONG counter, LONGLONG freq) {
  return (counter / freq) * 1000000000LL + (counter % freq) * 1000000000LL / freq;
}

ms_i64 platTimeMonotonicNs(void) {
  LARGE_INTEGER freq;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  return qpcToNs(counter.QuadPart, freq.QuadPart);
}

ms_i64 platTimeWallNs(void) {
  FILETIME ft;
  ULARGE_INTEGER unixEpoch;  // 1601-01-01 至 1970-01-01 的 100ns 刻度数
  GetSystemTimeAsFileTime(&ft);
  unixEpoch.LowPart = ft.dwLowDateTime;
  unixEpoch.HighPart = ft.dwHighDateTime;
  return (ms_i64)(unixEpoch.QuadPart - 116444736000000000ULL) * 100LL;
}

#else
#include <time.h>

static ms_i64 timespecToNs(const struct timespec* ts) {
  return (ms_i64)ts->tv_sec * 1000000000LL + ts->tv_nsec;
}

ms_i64 platTimeMonotonicNs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return timespecToNs(&ts);
}

ms_i64 platTimeWallNs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return timespecToNs(&ts);
}

#endif

ms_i64 platTimeMonotonicMs(void) { return platTimeMonotonicNs() / 1000000LL; }

ms_i64 platTimeWallMs(void) { return platTimeWallNs() / 1000000LL; }
```

- [ ] **Step 7: 写 src/plat/plat_io.h**（仅占位声明，reactor 实现归 T032）

```c
#ifndef INCLUDE_MS_PLAT_PLAT_IO_H_
#define INCLUDE_MS_PLAT_PLAT_IO_H_

// IO 多路复用 reactor 的实现归 T032（见 docs/language/20-architecture.md §4）
typedef struct PlatReactor PlatReactor;

#endif
```

- [ ] **Step 8: 跑测试确认通过（绿）**

Run: `clang-format -i src/plat/*.h src/plat/*.c tests/unit/test_common.c`
Run: `cmake --build build --config Debug`
Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: `100% tests passed`（含线程冒烟 4×10000 自增 == 40000）

- [ ] **Step 9: Commit**

```powershell
git add src/plat/ tests/unit/test_common.c
git commit -m "✨ feat(plat): add thread/atomic/time wrappers and io placeholder with tests"
```

---

### Task 4: CI 工作流与全量格式门禁

**Files:**
- Create: `.github/workflows/ci.yml`（项目目录内；GitHub 暂不执行——已与用户确认，DoD 的 CI 项标记待远端）

- [ ] **Step 1: 写 .github/workflows/ci.yml**（按 monorepo 根路径书写，未来拆库/根工作流可直接迁移）

```yaml
name: mslang-ci

on:
  push:
    paths: ["aicode/mslang-cc/**"]
  pull_request:
    paths: ["aicode/mslang-cc/**"]

defaults:
  run:
    working-directory: aicode/mslang-cc

jobs:
  build-test:
    strategy:
      fail-fast: false
      matrix:
        include:
          - { os: windows-latest, preset: msvc, config: Debug }
          - { os: windows-latest, preset: msvc, config: Release }
          - { os: ubuntu-latest, preset: debug }
          - { os: ubuntu-latest, preset: release }
          - { os: macos-latest, preset: debug }
          - { os: macos-latest, preset: release }
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - run: cmake --preset ${{ matrix.preset }}
      - if: matrix.config == ''
        run: cmake --build build
      - if: matrix.config != ''
        run: cmake --build build --config ${{ matrix.config }}
      - if: matrix.config == ''
        run: ctest --test-dir build --output-on-failure
      - if: matrix.config != ''
        run: ctest --test-dir build -C ${{ matrix.config }} --output-on-failure
  format:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y clang-format
      - name: clang-format check
        run: |
          find src tools tests \( -name '*.c' -o -name '*.h' \) -print0 |
            xargs -0 -n1 clang-format --dry-run --Werror
```

- [ ] **Step 2: 全量格式门禁（DoD）**

Run:
```powershell
$fail = 0
Get-ChildItem src, tools, tests -Recurse -Include *.c, *.h | ForEach-Object {
  clang-format --dry-run --Werror $_.FullName
  if ($LASTEXITCODE -ne 0) { $fail = 1 }
}
exit $fail
```
Expected: 无输出，退出码 `0`（若失败：对违例文件跑 `clang-format -i` 后重跑构建）

- [ ] **Step 3: 双配置重建 + ctest + ms version（DoD 汇总验证）**

Run: `cmake --build build --config Debug; cmake --build build --config Release`
Run: `ctest --test-dir build -C Debug --output-on-failure; ctest --test-dir build -C Release --output-on-failure`
Run: `& .\build\Release\ms.exe version; $LASTEXITCODE`
Expected: 两配置 0 警告 0 错误；ctest 均 `100% tests passed`；version 输出 `mslang 0.0.1 (dev)` 退出码 `0`

- [ ] **Step 4: build/ 隔离检查（DoD）**

Run: `git status --short`
Expected: 工作区无 `build/` 外的新增产物文件

- [ ] **Step 5: Commit**

```powershell
git add .github/workflows/ci.yml
git commit -m "👷 ci(build): add three-platform workflow and format gate"
```

---

### Task 5: 任务状态更新与终验

**Files:**
- Modify: `docs/tasks/T001-project-skeleton.md`（状态 → 已完成，勾选 DoD，备注 CI 待远端）
- Modify: `docs/tasks/README.md`（总表 T001 状态 ⬜ → ✅）

- [ ] **Step 1: 更新 T001 文档头部状态** `- 状态: 未开始` → `- 状态: 已完成`

- [ ] **Step 2: 勾选 DoD** 五项中：本地可验证四项勾选 `[x]`；CI 项保持 `[ ]` 并追加一行备注：`CI 工作流已就位（项目内路径，GitHub 暂不执行）；拆库或根工作流接入后补验`

- [ ] **Step 3: 更新索引** `docs/tasks/README.md` 总表 T001 行状态 `⬜` → `✅`

- [ ] **Step 4: Commit**

```powershell
git add docs/tasks/T001-project-skeleton.md docs/tasks/README.md
git commit -m "📝 docs(tasks): mark T001 done"
```

- [ ] **Step 5: 终验（verification-before-completion）**

复跑 Task 4 Step 2/3 全部命令并记录输出证据；`git log --oneline -6` 确认 5 个提交齐全。

---

## Self-Review 结论

- **Spec 覆盖**：T001 实现要点逐条对照——CMake 顶层 ✓（Step 1）、唯一可执行 ms ✓、computed goto 探测 ✓、common 四件套 ✓（Task 2）、plat 四类 ✓（Task 3）、.clang-format/.editorconfig/.gitignore ✓（Task 1）、CI ✓（Task 4，位置按用户决策）、DoD 五项映射到 Task 1/4/5 验证步骤 ✓、测试计划 4 用例全覆盖于 test_common.c ✓（定宽静态断言/数组宏/FNV 双向量化/线程冒烟）
- **占位符扫描**：无 TBD/TODO；所有代码步骤含完整代码
- **类型一致性**：`msFnv1a32/64`、`MS_ARRAY_PUSH`、`PlatThread/Mutex/Cond`、`PlatAtomic` 在测试与实现间签名一致；`common.h` Task 1 最小版 → Task 2 完整版为纯扩充，无破坏
- **已知限制（非本任务缺陷）**：asan 预设无法本地验证（无 GCC/Clang-Ninja 环境，CI 项待远端）；`/experimental:c11atomics` 为 MSVC 17.5-17.12 必需，MSVC 后续默认开启时需移除（留注释标记）
