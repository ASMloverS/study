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

ms_add_test(test_capi_registry unit/test_capi_registry.c)
target_link_libraries(test_capi_registry PRIVATE ms_frontend)

ms_add_test(test_capi_native_def unit/test_capi_native_def.c)
target_link_libraries(test_capi_native_def PRIVATE ms_frontend)

ms_add_test(test_natives unit/test_natives.c)
target_link_libraries(test_natives PRIVATE ms_frontend)

ms_add_test(test_optimize unit/test_optimize.c)
target_link_libraries(test_optimize PRIVATE ms_optimize ms_chunk)

ms_add_test(test_quickening unit/test_quickening.c)
target_link_libraries(test_quickening PRIVATE ms_frontend)

ms_add_test(test_serializer unit/test_serializer.c)
target_link_libraries(test_serializer PRIVATE ms_frontend ms_serializer)

ms_add_test(test_fs_util unit/test_fs_util.c)
target_link_libraries(test_fs_util PRIVATE ms_fs_util)

ms_add_test(test_async_scaffold unit/test_async_scaffold.c)
target_link_libraries(test_async_scaffold PRIVATE ms_scanner ms_support)

ms_add_test(test_async_frontend unit/test_async_frontend.c)
target_link_libraries(test_async_frontend PRIVATE ms_frontend)

ms_add_test(test_future unit/test_future.c)
target_link_libraries(test_future PRIVATE ms_frontend)

ms_add_test(test_event_loop unit/test_event_loop.c)
target_link_libraries(test_event_loop PRIVATE ms_frontend ms_event_loop)

ms_add_test(test_reactor unit/test_reactor.c)
target_link_libraries(test_reactor PRIVATE ms_reactor)

ms_add_test(test_tcp_socket unit/test_tcp_socket.c)
target_link_libraries(test_tcp_socket PRIVATE ms_frontend)

# Async fixture tests -- run .ms scripts via the interpreter and compare output
set(_async_fixtures
    async_basic
    async_await_resolved
    async_chain
    async_gather
    async_gather_empty
    async_multi_waiter
    async_reject_catch
    async_sleep_seq
    async_tcp_close
    async_tcp_echo
)
foreach(_f IN LISTS _async_fixtures)
    ms_add_fixture_test(
        "${PROJECT_SOURCE_DIR}/tests/fixtures/${_f}.ms"
        TIMEOUT 15
    )
endforeach()
