cmake_minimum_required(VERSION 3.20)
project(mslang-c LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # /wd4200: nonstandard extension used: zero-sized array in struct (C11 FAM)
    set(MSLANG_COMPILE_OPTIONS /W4 /WX /wd4200)
else()
    set(MSLANG_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Werror)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

add_library(ms_fs_util STATIC src/fs_util.c)
target_include_directories(ms_fs_util PUBLIC include)
target_compile_options(ms_fs_util PRIVATE ${MSLANG_COMPILE_OPTIONS})

add_library(ms_support STATIC src/value.c)
target_include_directories(ms_support PUBLIC include)
target_compile_options(ms_support PRIVATE ${MSLANG_COMPILE_OPTIONS})

add_library(ms_table STATIC src/table.c)
target_include_directories(ms_table PUBLIC include)
target_compile_options(ms_table PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_table PUBLIC ms_support)

add_library(ms_memory STATIC src/memory.c)
target_include_directories(ms_memory PUBLIC include)
target_compile_options(ms_memory PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_memory PUBLIC ms_support)

add_library(ms_chunk STATIC src/chunk.c)
target_include_directories(ms_chunk PUBLIC include)
target_compile_options(ms_chunk PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_chunk PUBLIC ms_memory)

add_library(ms_debug STATIC src/debug.c)
target_include_directories(ms_debug PUBLIC include)
target_compile_options(ms_debug PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_debug PUBLIC ms_chunk)

add_library(ms_vtable STATIC src/vtable.c)
target_include_directories(ms_vtable PUBLIC include)
target_compile_options(ms_vtable PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_vtable PUBLIC ms_support)

add_library(ms_object STATIC src/object.c)
target_include_directories(ms_object PUBLIC include)
target_compile_options(ms_object PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_object PUBLIC ms_table ms_vtable ms_memory ms_chunk)

add_library(ms_vm STATIC src/vm.c src/vm_call.c src/vm_natives.c src/vm_gc.c src/shape.c src/vm_builtins.c src/module.c)
target_include_directories(ms_vm PUBLIC include)
target_compile_options(ms_vm PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_vm PUBLIC ms_object)

add_library(ms_optimize STATIC src/optimize.c)
target_include_directories(ms_optimize PUBLIC include)
target_compile_options(ms_optimize PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_optimize PUBLIC ms_chunk)

add_library(ms_scanner STATIC src/scanner.c)
target_include_directories(ms_scanner PUBLIC include)
target_compile_options(ms_scanner PRIVATE ${MSLANG_COMPILE_OPTIONS})

add_library(ms_serializer STATIC src/serializer.c)
target_include_directories(ms_serializer PUBLIC include)
target_compile_options(ms_serializer PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_serializer PUBLIC ms_vm)

add_library(ms_frontend STATIC src/compiler.c src/compiler_expr.c)
target_include_directories(ms_frontend PUBLIC include PRIVATE src)
target_compile_options(ms_frontend PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(ms_frontend PUBLIC ms_vm ms_scanner ms_debug ms_optimize ms_serializer)

add_executable(mslang-c src/main.c)
target_include_directories(mslang-c PRIVATE include)
target_compile_options(mslang-c PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(mslang-c PRIVATE ms_frontend)

option(MSLANG_BUILD_BENCHMARKS "Build mslang-c benchmark suite" OFF)
option(MSLANG_VM_STATS         "Enable VM stats counters"        OFF)

if(MSLANG_VM_STATS)
    target_compile_definitions(ms_memory PUBLIC MSLANG_VM_STATS)
    target_compile_definitions(ms_vm PUBLIC MSLANG_VM_STATS)
    target_compile_definitions(mslang-c PRIVATE MSLANG_VM_STATS)
endif()

if(MSLANG_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

enable_testing()
add_subdirectory(tests)
