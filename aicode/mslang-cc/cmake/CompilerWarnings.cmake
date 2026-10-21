# 警告门禁：MSVC /W4 /WX；GCC/Clang -Wall -Wextra -Werror -Wshadow
# /utf-8：MSVC 源码按 UTF-8 解析（无 BOM 中文注释防 C4819）；/experimental:c11atomics：<stdatomic.h> 前置开关（VS ≥ 17.13 默认开启时移除）
if(MSVC)
  add_compile_options(/W4 /WX /utf-8 /experimental:c11atomics)
else()
  add_compile_options(-Wall -Wextra -Werror -Wshadow)
  if(NOT WIN32)
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
    add_compile_options(-pthread)
    add_link_options(-pthread)
  else()
    # clang（GNU 前端）+ MSVC ABI 下 UCRT 将 fopen/tmpfile/getenv 标记 deprecated，cl 静默而 -Werror 报错；定义宏对齐 MSVC 基线（T012 ASan 门禁）
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  endif()
endif()
