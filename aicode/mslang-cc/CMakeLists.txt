cmake_minimum_required(VERSION 3.16)

project(mslang C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(cmake/CompilerWarnings.cmake)

# 探测 computed goto：MSVC 从不支持 labels-as-values（cl 忽略 GCC 式 -std 参数，探测恒否），GCC/Clang 依探测结果定义 MS_USE_COMPUTED_GOTO（见 T011 dispatch 双路径）
include(CheckCSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state()
check_c_source_compiles("
int main(void) {
  static void* table[] = {&&label};
  goto* table[0];
label:
  return 0;
}
" MS_HAVE_COMPUTED_GOTO)
cmake_pop_check_state()
if(MS_HAVE_COMPUTED_GOTO)
  add_compile_definitions(MS_USE_COMPUTED_GOTO=1)
endif()

option(MS_SANITIZERS "Enable ASan+UBSan (GCC/Clang only)" OFF)
if(MS_SANITIZERS AND MSVC)
  message(FATAL_ERROR "MS_SANITIZERS requires GCC/Clang")
endif()
if(MS_SANITIZERS AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
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
  target_link_libraries(ms_${mod} PUBLIC ms_common)
endforeach()

# scanner：占位循环之外追加 T003 起的实际源文件（后续任务按目录继续 target_sources）
target_sources(ms_scanner PRIVATE src/scanner/token.c)

# T009：binder 名称解析/常量折叠；ast 规范化 dump
target_sources(ms_parser PRIVATE src/parser/ast_dump.c)
target_sources(ms_binder PRIVATE src/binder/binder.c)
target_link_libraries(ms_binder PUBLIC ms_parser)

# T010：指令集/Chunk/编译器基础；compiler 消费标注 AST（ast.h）
target_sources(ms_compiler PRIVATE src/compiler/chunk.c src/compiler/compiler.c src/compiler/disasm.c)
target_link_libraries(ms_compiler PUBLIC ms_parser)

# T011：VM 值/内置桥；vm 消费 chunk（chunk.h/行号表/指令元数据）
target_sources(ms_vm PRIVATE src/vm/value.c src/vm/builtins_bridge.c)
target_link_libraries(ms_vm PUBLIC ms_compiler)

# T012：对象头/分配器 + 过渡 GC；obj 分配经 guard（gc）、vm 持有 heap 生命周期
target_link_libraries(ms_obj PRIVATE ms_gc)
target_link_libraries(ms_vm PRIVATE ms_gc)

# T006：parser 逐 token 拉取，依赖 scanner
target_link_libraries(ms_parser PUBLIC ms_scanner)

# T004：Unicode 类别表再生成（生成物已入库；改脚本/升 Python 后手动构建本目标）
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_custom_target(gen_unicode_table
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_unicode_table.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "regenerate src/scanner/unicode_table.h"
    VERBATIM)
endif()

add_executable(ms tools/ms.c)
target_link_libraries(
  ms PRIVATE ms_common ms_plat ms_scanner ms_parser ms_binder ms_compiler ms_vm ms_obj ms_gc ms_sched
  ms_reactor ms_module ms_ms_api ms_stdlib
)

enable_testing()
include(cmake/Tests.cmake)
add_subdirectory(tests)
