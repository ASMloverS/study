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

ms_add_test(test_vm_closures unit/test_vm_closures.c)
target_link_libraries(test_vm_closures PRIVATE ms_frontend)

ms_add_test(test_vm_control unit/test_vm_control.c)
target_link_libraries(test_vm_control PRIVATE ms_frontend)

ms_add_test(test_gc unit/test_gc.c)
target_link_libraries(test_gc PRIVATE ms_frontend)

ms_add_test(test_gc_gen unit/test_gc_gen.c)
target_link_libraries(test_gc_gen PRIVATE ms_frontend)

ms_add_test(test_oop unit/test_oop.c)
target_link_libraries(test_oop PRIVATE ms_frontend)

ms_add_test(test_oop_inherit unit/test_oop_inherit.c)
target_link_libraries(test_oop_inherit PRIVATE ms_frontend)

ms_add_test(test_shapes unit/test_shapes.c)
target_link_libraries(test_shapes PRIVATE ms_frontend)

ms_add_test(test_collections unit/test_collections.c)
target_link_libraries(test_collections PRIVATE ms_frontend)

ms_add_test(test_builtins unit/test_builtins.c)
target_link_libraries(test_builtins PRIVATE ms_frontend)

ms_add_test(test_interp_exec unit/test_interp_exec.c)
target_link_libraries(test_interp_exec PRIVATE ms_frontend)

ms_add_test(test_exceptions unit/test_exceptions.c)
target_link_libraries(test_exceptions PRIVATE ms_frontend)

ms_add_test(test_coroutines unit/test_coroutines.c)
target_link_libraries(test_coroutines PRIVATE ms_frontend)

ms_add_test(test_modules unit/test_modules.c)
target_link_libraries(test_modules PRIVATE ms_frontend)

ms_add_test(test_natives unit/test_natives.c)
target_link_libraries(test_natives PRIVATE ms_frontend)

ms_add_test(test_optimize unit/test_optimize.c)
target_link_libraries(test_optimize PRIVATE ms_optimize ms_chunk)

ms_add_test(test_quickening unit/test_quickening.c)
target_link_libraries(test_quickening PRIVATE ms_frontend)

ms_add_test(test_serializer unit/test_serializer.c)
target_link_libraries(test_serializer PRIVATE ms_frontend ms_serializer)
