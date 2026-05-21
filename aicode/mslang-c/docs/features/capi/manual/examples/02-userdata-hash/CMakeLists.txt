add_library(djb2 SHARED hash_ext.c)
target_include_directories(djb2 PRIVATE ${MSLANG_INCLUDE_DIR})
