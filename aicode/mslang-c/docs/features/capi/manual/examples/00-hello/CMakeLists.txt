add_library(hello SHARED hello.c)
target_include_directories(hello PRIVATE ${MSLANG_INCLUDE_DIR})
