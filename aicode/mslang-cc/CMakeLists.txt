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

add_executable(ms tools/ms.c)
target_link_libraries(
  ms PRIVATE ms_common ms_plat ms_scanner ms_parser ms_binder ms_compiler ms_vm ms_obj ms_gc ms_sched
  ms_reactor ms_module ms_ms_api ms_stdlib
)

enable_testing()
include(cmake/Tests.cmake)
add_subdirectory(tests)
