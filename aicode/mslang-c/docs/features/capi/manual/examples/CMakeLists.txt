cmake_minimum_required(VERSION 3.20)
project(mslang-c-examples LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default: project root is five levels up from this file.
# Override with: cmake -DMSLANG_INCLUDE_DIR=/path/to/mslang-c/include ...
set(MSLANG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../include"
    CACHE PATH "Path to mslang-c include/ directory")

add_subdirectory(00-hello)
add_subdirectory(01-values-errors)
# Uncomment as each example is added:
# add_subdirectory(02-userdata-hash)
# add_subdirectory(03-async-fileread)
# add_subdirectory(04-bytes-interop)
