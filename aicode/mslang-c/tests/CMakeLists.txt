include(MslangTesting)

ms_add_test(test_smoke unit/test_smoke.c)

ms_add_test(test_value unit/test_value.c)
target_link_libraries(test_value PRIVATE ms_support)

ms_add_test(test_table unit/test_table.c)
target_link_libraries(test_table PRIVATE ms_table)

ms_add_test(test_object_string unit/test_object_string.c)
target_link_libraries(test_object_string PRIVATE ms_vm)

ms_add_test(test_base_objects unit/test_base_objects.c)
target_link_libraries(test_base_objects PRIVATE ms_vm)

ms_add_test(test_chunk unit/test_chunk.c)
target_link_libraries(test_chunk PRIVATE ms_chunk)

ms_add_test(test_debug unit/test_debug.c)
target_link_libraries(test_debug PRIVATE ms_debug)
