# T01: `fs_util` 跨平台文件系统工具模块

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新建独立 `ms_fs_util` 静态库，提供 `ms_fs_stat` / `ms_fs_mkdir` / `ms_fs_atomic_rename` / `ms_fs_unlink` 四个跨平台原语，供后续所有缓存任务使用。

**Architecture:** 纯新增，零改动现有代码。Windows 用 `GetFileAttributesExA` / `_mkdir` / `MoveFileExA`，POSIX 用 `stat` / `mkdir` / `rename`。先写测试、验证编译失败，再实现，再验证通过。

**Tech Stack:** C11, CMake `add_library(STATIC)`, `_WIN32` / POSIX 条件编译

---

## Files

- Create: `include/ms/fs_util.h`
- Create: `src/fs_util.c`
- Create: `tests/unit/test_fs_util.c`
- Modify: `CMakeLists.txt` — 新增 `ms_fs_util` 静态库
- Modify: `tests/CMakeLists.txt` — 注册 `test_fs_util`

---

- [x] **Step 1: 写失败测试**

新建 `tests/unit/test_fs_util.c`：

```c
#include "test_assert.h"
#include "ms/fs_util.h"
#include <stdio.h>

#ifdef _WIN32
#  include <direct.h>
#  define ms_rmdir _rmdir
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  define ms_rmdir rmdir
#endif

static void test_stat_existing(void) {
    FILE* f = fopen("_t01_src.tmp", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("hello", 1, 5, f);
    fclose(f);

    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("_t01_src.tmp", &m));
    TEST_ASSERT_EQ((int)m.size, 5);
    TEST_ASSERT(m.mtime_ns != 0);
    remove("_t01_src.tmp");
}

static void test_stat_missing(void) {
    MsFileMeta m;
    TEST_ASSERT(!ms_fs_stat("_t01_no_such_file.tmp", &m));
}

static void test_mkdir_creates_and_eexist_ok(void) {
    ms_rmdir("_t01_testdir");
    TEST_ASSERT(ms_fs_mkdir("_t01_testdir"));
    TEST_ASSERT(ms_fs_mkdir("_t01_testdir"));  /* EEXIST → still true */
    ms_rmdir("_t01_testdir");
}

static void test_atomic_rename(void) {
    FILE* f = fopen("_t01_rename_src.tmp", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("data", 1, 4, f);
    fclose(f);

    TEST_ASSERT(ms_fs_atomic_rename("_t01_rename_src.tmp", "_t01_rename_dst.tmp"));

    MsFileMeta m;
    TEST_ASSERT(!ms_fs_stat("_t01_rename_src.tmp", &m));
    TEST_ASSERT(ms_fs_stat("_t01_rename_dst.tmp", &m));
    TEST_ASSERT_EQ((int)m.size, 4);
    remove("_t01_rename_dst.tmp");
}

static void test_atomic_rename_replaces_existing(void) {
    FILE* f = fopen("_t01_old.tmp", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("old", 1, 3, f);
    fclose(f);

    f = fopen("_t01_new.tmp", "wb");
    TEST_ASSERT(f != NULL);
    fwrite("new!", 1, 4, f);
    fclose(f);

    TEST_ASSERT(ms_fs_atomic_rename("_t01_new.tmp", "_t01_old.tmp"));

    MsFileMeta m;
    TEST_ASSERT(ms_fs_stat("_t01_old.tmp", &m));
    TEST_ASSERT_EQ((int)m.size, 4);
    remove("_t01_old.tmp");
}

static void test_unlink(void) {
    FILE* f = fopen("_t01_unlink.tmp", "wb");
    TEST_ASSERT(f != NULL);
    fclose(f);
    TEST_ASSERT(ms_fs_unlink("_t01_unlink.tmp"));
    MsFileMeta m;
    TEST_ASSERT(!ms_fs_stat("_t01_unlink.tmp", &m));
}

int main(void) {
    test_stat_existing();
    test_stat_missing();
    test_mkdir_creates_and_eexist_ok();
    test_atomic_rename();
    test_atomic_rename_replaces_existing();
    test_unlink();
    printf("test_fs_util: all passed\n");
    return 0;
}
```

- [x] **Step 2: 注册测试到 `tests/CMakeLists.txt`**

在末尾追加：

```cmake
ms_add_test(test_fs_util unit/test_fs_util.c)
target_link_libraries(test_fs_util PRIVATE ms_fs_util)
```

- [x] **Step 3: 运行，确认编译失败（预期）**

```bash
cmake --build build
```

预期输出：`fatal error: ms/fs_util.h: No such file or directory`

- [x] **Step 4: 创建 `include/ms/fs_util.h`**

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t size;
    int64_t  mtime_ns;  /* nanoseconds since Unix epoch, Y2038-safe */
} MsFileMeta;

bool ms_fs_stat(const char* path, MsFileMeta* out);
bool ms_fs_mkdir(const char* path);                          /* EEXIST → true */
bool ms_fs_atomic_rename(const char* tmp, const char* dst); /* replaces dst */
bool ms_fs_unlink(const char* path);
```

- [x] **Step 5: 创建 `src/fs_util.c`**

```c
#include "ms/fs_util.h"
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>  /* _mkdir  */
#include <io.h>      /* _unlink */

bool ms_fs_stat(const char* path, MsFileMeta* out) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return false;
    ULARGE_INTEGER sz;
    sz.LowPart  = d.nFileSizeLow;
    sz.HighPart = d.nFileSizeHigh;
    out->size = sz.QuadPart;
    ULARGE_INTEGER ft;
    ft.LowPart  = d.ftLastWriteTime.dwLowDateTime;
    ft.HighPart = d.ftLastWriteTime.dwHighDateTime;
    /* FILETIME: 100-ns ticks since 1601-01-01; 116444736000000000 ticks to Unix epoch */
    out->mtime_ns = (int64_t)(ft.QuadPart - 116444736000000000ULL) * 100LL;
    return true;
}

bool ms_fs_mkdir(const char* path) {
    return _mkdir(path) == 0 || errno == EEXIST;
}

bool ms_fs_atomic_rename(const char* tmp, const char* dst) {
    return MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING) != 0;
}

bool ms_fs_unlink(const char* path) {
    return _unlink(path) == 0;
}

#else
#include <sys/stat.h>
#include <unistd.h>

bool ms_fs_stat(const char* path, MsFileMeta* out) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    out->size = (uint64_t)st.st_size;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    out->mtime_ns = (int64_t)st.st_mtimespec.tv_sec * 1000000000LL
                  + (int64_t)st.st_mtimespec.tv_nsec;
#else
    out->mtime_ns = (int64_t)st.st_mtim.tv_sec * 1000000000LL
                  + (int64_t)st.st_mtim.tv_nsec;
#endif
    return true;
}

bool ms_fs_mkdir(const char* path) {
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool ms_fs_atomic_rename(const char* tmp, const char* dst) {
    return rename(tmp, dst) == 0;
}

bool ms_fs_unlink(const char* path) {
    return unlink(path) == 0;
}
#endif
```

- [x] **Step 6: 注册 `ms_fs_util` 到根 `CMakeLists.txt`**

在 `add_library(ms_support ...)` 行**之前**插入：

```cmake
add_library(ms_fs_util STATIC src/fs_util.c)
target_include_directories(ms_fs_util PUBLIC include)
target_compile_options(ms_fs_util PRIVATE ${MSLANG_COMPILE_OPTIONS})
```

- [x] **Step 7: 构建 + 运行测试**

```bash
cmake --build build && cd build && ctest -R test_fs_util --output-on-failure
```

预期输出：

```
test_fs_util: all passed
1/1 Test #1: test_fs_util .................... Passed
```

- [x] **Step 8: 确认全量测试不回归**

```bash
cd build && ctest --output-on-failure
```

预期：所有原有测试继续通过。

- [x] **Step 9: Commit**

```bash
git add include/ms/fs_util.h src/fs_util.c \
        tests/unit/test_fs_util.c \
        CMakeLists.txt tests/CMakeLists.txt
git commit -m "✨ feat(cache): add cross-platform fs_util module (stat/mkdir/rename/unlink)"
```
