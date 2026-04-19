include(MslangTesting)

ms_add_test(test_smoke unit/test_smoke.c)

ms_add_test(test_value unit/test_value.c)
target_link_libraries(test_value PRIVATE ms_support)

ms_add_test(test_table unit/test_table.c)
target_link_libraries(test_table PRIVATE ms_table)

ms_add_test(test_object_string unit/test_object_string.c)
target_link_libraries(test_object_string PRIVATE ms_frontend)

ms_add_test(test_base_objects unit/test_base_objects.c)
target_link_libraries(test_base_objects PRIVATE ms_frontend)

ms_add_test(test_chunk unit/test_chunk.c)
target_link_libraries(test_chunk PRIVATE ms_chunk)

ms_add_test(test_debug unit/test_debug.c)
target_link_libraries(test_debug PRIVATE ms_debug)

ms_add_test(test_scanner unit/test_scanner.c)
target_link_libraries(test_scanner PRIVATE ms_scanner)

ms_add_test(test_scanner_interp unit/test_scanner_interp.c)
target_link_libraries(test_scanner_interp PRIVATE ms_scanner)

ms_add_test(test_compiler_expr unit/test_compiler_expr.c)
target_link_libraries(test_compiler_expr PRIVATE ms_frontend)

ms_add_test(test_compiler_vars unit/test_compiler_vars.c)
target_link_libraries(test_compiler_vars PRIVATE ms_frontend)

ms_add_test(test_compiler_stmts unit/test_compiler_stmts.c)
target_link_libraries(test_compiler_stmts PRIVATE ms_frontend)

ms_add_test(test_vm_basic unit/test_vm_basic.c)
target_link_libraries(test_vm_basic PRIVATE ms_frontend)
