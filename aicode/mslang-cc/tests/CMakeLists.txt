add_executable(test_common unit/test_common.c)
target_link_libraries(test_common PRIVATE ms_common ms_plat)
add_test(NAME common COMMAND test_common)
