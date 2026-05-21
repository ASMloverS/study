add_library(bufops SHARED bufops.c)
target_include_directories(bufops PRIVATE ${MSLANG_INCLUDE_DIR})
