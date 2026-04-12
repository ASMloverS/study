include(MslangTesting)

ms_add_test(test_smoke unit/test_smoke.c)

ms_add_test(test_value unit/test_value.c)
target_link_libraries(test_value PRIVATE ms_support)

ms_add_test(test_table unit/test_table.c)
target_link_libraries(test_table PRIVATE ms_table)
