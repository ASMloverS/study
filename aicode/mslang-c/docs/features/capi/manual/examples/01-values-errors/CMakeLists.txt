add_library(greet SHARED greet.c)
target_include_directories(greet PRIVATE ${MSLANG_INCLUDE_DIR})
