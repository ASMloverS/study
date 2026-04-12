cmake_minimum_required(VERSION 3.20)
project(mslang-c LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(MSLANG_COMPILE_OPTIONS /W4 /WX)
else()
    set(MSLANG_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Werror)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

add_library(ms_support STATIC src/value.c)
target_include_directories(ms_support PUBLIC include)
target_compile_options(ms_support PRIVATE ${MSLANG_COMPILE_OPTIONS})

add_executable(mslang-c src/main.c)
target_include_directories(mslang-c PRIVATE include)
target_compile_options(mslang-c PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(mslang-c PRIVATE ms_support)

enable_testing()
add_subdirectory(tests)
