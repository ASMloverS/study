# T01: Project Skeleton & Build System

**Phase**: 1 · **Deps**: none · **Complexity**: low · **Status**: ✅ COMPLETED

## Goal

Dir structure + CMake config + minimal `main.c` → build pipeline for all later tasks.

## Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root build config |
| `src/main.c` | Entry point stub |
| `src/common.h` | Definitions placeholder |
| `include/` | Include dir (empty) |
| `tests/CMakeLists.txt` | Test build placeholder |
| `tests/unit/` | Unit tests (empty) |
| `tests/basic/` | Integration tests (empty) |
| `examples/` | Example scripts (empty) |
| `.gitignore` | Ignore `build/`, binaries |

## TDD Cycles

No test framework yet → each "test" = cmake configure + compile + run.

### Cycle 1: CMake + Empty Main

**RED**: `CMakeLists.txt` refs `src/main.c` but file missing → build fails.
```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
Expected: compile/link error — `main.c` not found or no `main` symbol.

**GREEN**: Create `src/main.c`:
```c
#include <stdio.h>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return 0;
}
```

Create `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.10)
project(Maple LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

file(GLOB_RECURSE SOURCES "src/*.c")
add_executable(maple ${SOURCES})
target_include_directories(maple PRIVATE src)

option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```
Verify: `cmake -B build … && cmake --build build` → compiles clean.

**REFACTOR**: Zero warnings with `-Wall -Wextra -Wpedantic` / `/W4`. `(void)argc; (void)argv;` suppresses unused-param warnings.

### Cycle 2: Version Output

**RED**: `./build/maple` → empty stdout, expect version string.

**GREEN**: Update `src/main.c`:
```c
#include <stdio.h>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    printf("Maple Scripting Language v0.1\n");
    return 0;
}
```
Verify: `./build/maple` → prints `Maple Scripting Language v0.1`, exit 0.

**REFACTOR**: none.

### Cycle 3: Common Header + Test Infra

**RED**: `add_subdirectory(tests)` → CMake error: `tests/CMakeLists.txt` missing.

**GREEN**:
- `src/common.h` with include guards:
```c
#ifndef MS_COMMON_H
#define MS_COMMON_H

#endif
```
- `tests/CMakeLists.txt` — empty file.
- Empty dirs + `.gitkeep`: `tests/unit/`, `tests/basic/`, `examples/`, `include/`.
- `.gitignore`:
```
build/
*.o
*.obj
*.exe
*.out
```
Verify: configure + build + run → version string prints.

**REFACTOR**: Structure matches REQUIREMENTS.md §2.3.1. Zero warnings.

## Acceptance Criteria

- [x] `cmake -B build` succeeds
- [x] `cmake --build build` — zero errors
- [x] `./build/maple` prints "Maple Scripting Language v0.1"
- [x] Dir structure matches REQUIREMENTS.md §2.3.1
- [x] Zero warnings: `-Wall -Wextra -Wpedantic` / `/W4`

## Notes

- No test framework yet → "test" = build pipeline.
- `common.h` empty → filled T02.
- `tests/CMakeLists.txt` empty → populated T02.
