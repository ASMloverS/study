add_library(fileread SHARED fileread.c)
target_include_directories(fileread PRIVATE ${MSLANG_INCLUDE_DIR})
