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
