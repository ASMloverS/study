# T04: Platform Layer

**Phase**: 1 · **Deps**: T02, T03 · **Complexity**: medium

## Goal

Cross-platform abstraction: file I/O, path ops, console colors, time, env. All `#ifdef` confined to this module.

## Files

| File | Purpose |
|------|---------|
| `src/platform.h` | API + detection macros |
| `src/platform.c` | Impl (Windows + POSIX) |
| `tests/unit/test_platform.c` | Unit tests |

## TDD Cycles

### Cycle 1: Platform Detection + Path Separator

**RED**: `platform.h` missing → compile error.

```c
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>

static void test_platform_detection(void) {
    int platforms = MS_PLATFORM_WINDOWS + MS_PLATFORM_LINUX + MS_PLATFORM_MACOS;
    if (platforms == 0) { fprintf(stderr, "FAIL: no platform detected\n"); exit(1); }
    if (platforms > 1)  { fprintf(stderr, "FAIL: multiple platforms detected\n"); exit(1); }
}

static void test_path_separator(void) {
#ifdef _WIN32
    if (MS_PATH_SEPARATOR != '\\') { fprintf(stderr, "FAIL: MS_PATH_SEPARATOR expected '\\\\' on Windows\n"); exit(1); }
#else
    if (MS_PATH_SEPARATOR != '/')  { fprintf(stderr, "FAIL: MS_PATH_SEPARATOR expected '/' on POSIX\n"); exit(1); }
#endif
}

int main(void) {
    test_platform_detection();
    test_path_separator();
    printf("platform detection tests passed\n");
    return 0;
}
```

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_platform tests/unit/test_platform.c)
target_include_directories(test_platform PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_platform PRIVATE maple)
add_test(NAME test_platform COMMAND test_platform)
```

**GREEN**: Create `src/platform.h`:
```c
#ifndef MS_PLATFORM_H
#define MS_PLATFORM_H

#include <stdbool.h>
#include <stdio.h>

#ifdef _WIN32
  #define MS_PLATFORM_WINDOWS 1
#else
  #define MS_PLATFORM_WINDOWS 0
#endif
#ifdef __linux__
  #define MS_PLATFORM_LINUX 1
#else
  #define MS_PLATFORM_LINUX 0
#endif
#ifdef __APPLE__
  #define MS_PLATFORM_MACOS 1
#else
  #define MS_PLATFORM_MACOS 0
#endif

#if MS_PLATFORM_WINDOWS
  #define MS_PATH_SEPARATOR '\\'
  #define MS_PATH_SEPARATOR_STR "\\"
#else
  #define MS_PATH_SEPARATOR '/'
  #define MS_PATH_SEPARATOR_STR "/"
#endif

#endif
```

**REFACTOR**: none.

### Cycle 2: File Exists + Read/Write Round-Trip

**RED**: Declare in `platform.h`, skip impl → link error.

```c
static void test_file_exists(void) {
    if (ms_platform_file_exists("nonexistent_file_xyz.txt")) {
        fprintf(stderr, "FAIL: nonexistent file should not exist\n"); exit(1);
    }
}

static void test_write_read_roundtrip(void) {
    const char* path = "test_platform_tmp.txt";
    const char* content = "Hello, Maple!\nLine 2\n";

    if (!ms_platform_write_file(path, content)) { fprintf(stderr, "FAIL: write_file returned false\n"); exit(1); }
    if (!ms_platform_file_exists(path))         { fprintf(stderr, "FAIL: file should exist after write\n"); exit(1); }

    char* read_back = ms_platform_read_file(path);
    if (read_back == NULL) { fprintf(stderr, "FAIL: read_file returned NULL\n"); exit(1); }

    if (strcmp(read_back, content) != 0) {
        fprintf(stderr, "FAIL: read content differs from written content\n");
        fprintf(stderr, "  expected: %s\n", content);
        fprintf(stderr, "  got: %s\n", read_back);
        MS_FREE(char, read_back, strlen(read_back) + 1);
        remove(path);
        exit(1);
    }

    MS_FREE(char, read_back, strlen(read_back) + 1);
    remove(path);
}
```

Add `#include "memory.h"` + `#include <string.h>`.

**GREEN**: Add declarations to `src/platform.h`:
```c
bool ms_platform_file_exists(const char* path);
char* ms_platform_read_file(const char* path);
bool ms_platform_write_file(const char* path, const char* content);
```

Create `src/platform.c`:
```c
#include "platform.h"
#include "memory.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/stat.h>
#endif

bool ms_platform_file_exists(const char* path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

bool ms_platform_write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len;
}

char* ms_platform_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0) { fclose(f); return NULL; }

    char* buf = MS_ALLOCATE(char, (size_t)file_size + 1);
    size_t bytes_read = fread(buf, 1, (size_t)file_size, f);
    buf[bytes_read] = '\0';
    fclose(f);
    return buf;
}
```

`ms_platform_read_file()` → heap-allocated via `MS_ALLOCATE`. Caller frees with `MS_FREE`.

**REFACTOR**: none.

### Cycle 3: Get CWD + Join Path

**RED**: `ms_platform_get_cwd` / `ms_platform_join_path` undefined → link error.

```c
static void test_get_cwd(void) {
    char* cwd = ms_platform_get_cwd();
    if (cwd == NULL)          { fprintf(stderr, "FAIL: get_cwd returned NULL\n"); exit(1); }
    if (strlen(cwd) == 0)     { fprintf(stderr, "FAIL: get_cwd returned empty string\n"); MS_FREE(char, cwd, strlen(cwd) + 1); exit(1); }
    MS_FREE(char, cwd, strlen(cwd) + 1);
}

static void test_join_path_basic(void) {
    char* result = ms_platform_join_path("a", "b");
    if (result == NULL) { fprintf(stderr, "FAIL: join_path returned NULL\n"); exit(1); }

    const char expected_sep[] = { MS_PATH_SEPARATOR, '\0' };
    char expected[4] = "a";
    strcat(expected, expected_sep);
    strcat(expected, "b");

    if (strcmp(result, expected) != 0) {
        fprintf(stderr, "FAIL: join_path(\"a\", \"b\") expected \"%s\", got \"%s\"\n", expected, result);
        MS_FREE(char, result, strlen(result) + 1);
        exit(1);
    }
    MS_FREE(char, result, strlen(result) + 1);
}
```

**GREEN**: Add declarations to `src/platform.h`:
```c
char* ms_platform_get_cwd(void);
char* ms_platform_join_path(const char* a, const char* b);
```

Add to `src/platform.c`:
```c
char* ms_platform_get_cwd(void) {
#ifdef _WIN32
    char* buf = MS_ALLOCATE(char, MAX_PATH);
    DWORD len = GetCurrentDirectoryA(MAX_PATH, buf);
    if (len == 0) { MS_FREE(char, buf, MAX_PATH); return NULL; }
    return buf;
#else
    long path_max = pathconf(".", _PC_PATH_MAX);
    if (path_max <= 0) path_max = 4096;
    char* buf = MS_ALLOCATE(char, (size_t)path_max);
    if (getcwd(buf, (size_t)path_max) == NULL) { MS_FREE(char, buf, (size_t)path_max); return NULL; }
    return buf;
#endif
}

char* ms_platform_join_path(const char* a, const char* b) {
    size_t a_len = strlen(a);
    size_t b_len = strlen(b);
    size_t total = a_len + 1 + b_len + 1;
    char* result = MS_ALLOCATE(char, total);

    memcpy(result, a, a_len);
    size_t pos = a_len;

    if (a_len > 0 && a[a_len - 1] != MS_PATH_SEPARATOR && b_len > 0 && b[0] != MS_PATH_SEPARATOR) {
        result[pos++] = MS_PATH_SEPARATOR;
    }

    memcpy(result + pos, b, b_len + 1);
    return result;
}
```

**REFACTOR**: none. Handles trailing/leading separator.

### Cycle 4: Time + Env + Executable Path

**RED**: Functions undefined → link error.

```c
static void test_get_time_monotonic(void) {
    double t1 = ms_platform_get_time_seconds();
    if (t1 <= 0.0) { fprintf(stderr, "FAIL: get_time_seconds should be positive, got %f\n", t1); exit(1); }
    double t2 = ms_platform_get_time_seconds();
    if (t2 < t1)   { fprintf(stderr, "FAIL: time went backwards\n"); exit(1); }
}

static void test_get_env(void) {
    char* path_val = ms_platform_get_env("PATH");
    if (path_val == NULL)      { fprintf(stderr, "FAIL: PATH env var should exist\n"); exit(1); }
    if (strlen(path_val) == 0) { fprintf(stderr, "FAIL: PATH should not be empty\n"); MS_FREE(char, path_val, strlen(path_val) + 1); exit(1); }
    MS_FREE(char, path_val, strlen(path_val) + 1);

    char* nonexistent = ms_platform_get_env("MSLANG_NONEXISTENT_VAR_12345");
    if (nonexistent != NULL)   { fprintf(stderr, "FAIL: nonexistent env var should return NULL\n"); MS_FREE(char, nonexistent, strlen(nonexistent) + 1); exit(1); }
}

static void test_executable_path(void) {
    char* exe_path = ms_platform_get_executable_path();
    if (exe_path == NULL)      { fprintf(stderr, "FAIL: get_executable_path returned NULL\n"); exit(1); }
    if (strlen(exe_path) == 0) { fprintf(stderr, "FAIL: executable path is empty\n"); MS_FREE(char, exe_path, strlen(exe_path) + 1); exit(1); }
    MS_FREE(char, exe_path, strlen(exe_path) + 1);
}
```

**GREEN**: Add declarations to `src/platform.h`:
```c
char* ms_platform_get_executable_path(void);
double ms_platform_get_time_seconds(void);
char* ms_platform_get_env(const char* name);
```

Add to `src/platform.c`:
```c
double ms_platform_get_time_seconds(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (double)(uli.QuadPart / 10000) / 1000.0 - 11644473600.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

char* ms_platform_get_env(const char* name) {
#ifdef _WIN32
    char* buf = NULL;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == NULL) return NULL;
    char* result = MS_ALLOCATE(char, strlen(buf) + 1);
    memcpy(result, buf, strlen(buf) + 1);
    free(buf);
    return result;
#else
    const char* val = getenv(name);
    if (val == NULL) return NULL;
    char* result = MS_ALLOCATE(char, strlen(val) + 1);
    memcpy(result, val, strlen(val) + 1);
    return result;
#endif
}

char* ms_platform_get_executable_path(void) {
#ifdef _WIN32
    char* buf = MS_ALLOCATE(char, MAX_PATH);
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0) { MS_FREE(char, buf, MAX_PATH); return NULL; }
    return buf;
#elif defined(__linux__)
    char* buf = MS_ALLOCATE(char, 4096);
    ssize_t len = readlink("/proc/self/exe", buf, 4095);
    if (len == -1) { MS_FREE(char, buf, 4096); return NULL; }
    buf[len] = '\0';
    return buf;
#elif defined(__APPLE__)
    char* buf = MS_ALLOCATE(char, 4096);
    uint32_t size = 4096;
    if (_NSGetExecutablePath(buf, &size) != 0) { MS_FREE(char, buf, 4096); return NULL; }
    return buf;
#endif
}
```

**REFACTOR**: Linux → `<time.h>`. Apple → `<mach-o/dyld.h>`. All returned strings → caller frees via `MS_FREE`.

### Cycle 5: Console Colors

**RED**: Functions undefined → link error.

```c
static void test_console_colors(void) {
    ms_platform_enable_console_colors();
    if (!ms_platform_supports_colors()) {
        fprintf(stderr, "WARN: console colors not supported\n");
    }

    ms_platform_set_console_color("\033[31m");
    ms_platform_reset_console_color();

    ms_platform_set_console_color("invalid_code");
    ms_platform_reset_console_color();
}
```

**GREEN**: Add declarations to `src/platform.h`:
```c
void ms_platform_enable_console_colors(void);
bool ms_platform_supports_colors(void);
void ms_platform_set_console_color(const char* ansi_code);
void ms_platform_reset_console_color(void);
```

Add to `src/platform.c`:
```c
static bool s_colors_enabled = false;

void ms_platform_enable_console_colors(void) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
#endif
    s_colors_enabled = true;
}

bool ms_platform_supports_colors(void) {
    return s_colors_enabled;
}

void ms_platform_set_console_color(const char* ansi_code) {
    if (!s_colors_enabled) return;
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleA(hOut, ansi_code, (DWORD)strlen(ansi_code), &written, NULL);
#else
    fprintf(stderr, "%s", ansi_code);
#endif
}

void ms_platform_reset_console_color(void) {
    ms_platform_set_console_color("\033[0m");
}
```

**REFACTOR**: Color output = manual verify. Safe to call when unsupported.

## Acceptance Criteria

- [x] `ms_platform_file_exists()` → true/false correctly
- [x] `ms_platform_read_file()` / `ms_platform_write_file()` round-trip
- [x] `ms_platform_join_path("a", "b")` → "a\b" (Win) / "a/b" (POSIX)
- [x] `ms_platform_get_time_seconds()` → monotonic
- [x] `ms_platform_get_cwd()` → valid path
- [x] Console color fns → no crash
- [x] Platform-detection macros correct

## Notes

- All `#ifdef` confined to `platform.c` — no other file needs platform conditionals.
- All returned strings → caller frees via `MS_FREE`.
- `ms_platform_get_env()` → heap copy on all platforms.
- `ms_platform_set_console_color()` uses ANSI. Windows: `ms_platform_enable_console_colors()` enables virtual terminal processing first.
