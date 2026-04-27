mslangc_add_smoke_test(smoke.help
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/smoke_help_test.cmake")

function(mslangc_add_cli_test test_name)
  add_test(NAME "${test_name}"
    COMMAND "${CMAKE_COMMAND}" ${ARGN})
  set_tests_properties("${test_name}" PROPERTIES
    LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};runner")
endfunction()

function(mslangc_add_e2e_basic_test test_name script_name)
  add_test(NAME "${test_name}"
    COMMAND "${CMAKE_COMMAND}"
      -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
      -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/basic/${script_name}"
      -DEXPECT_EXIT=0
      -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/basic/${script_name}.out"
      -DEXPECT_STDERR=""
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
  set_tests_properties("${test_name}" PROPERTIES
    LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};e2e_basic")
endfunction()

function(mslangc_add_e2e_named_test test_name script_path output_path test_label)
  add_test(NAME "${test_name}"
    COMMAND "${CMAKE_COMMAND}"
      -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
      -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/${script_path}"
      -DEXPECT_EXIT=0
      -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/${output_path}"
      -DEXPECT_STDERR=""
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
  set_tests_properties("${test_name}" PROPERTIES
    LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};${test_label}")
endfunction()

add_executable(containers_e2e_runner containers_e2e_runner.c)
target_link_libraries(containers_e2e_runner PRIVATE
  mslang_frontend
  mslang_runtime_core)

mslangc_add_cli_test(runner_basic.inline
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DINLINE_SOURCE="print 7"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT="7\n"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(runner_basic.script
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/basic/smoke.ms"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/basic/smoke.ms.out"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(runner_basic.parse_error
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/basic/parse_error.ms"
  -DEXPECT_EXIT=65
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=parse;MS2001"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(runner_basic.resolve_error
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/basic/resolve_error.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=resolve;MS3003"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(runner_basic.runtime_error
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/basic/runtime_error.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;MS4004"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

add_subdirectory(unit)

mslangc_add_e2e_basic_test(e2e_basic.smoke smoke.ms)
mslangc_add_e2e_basic_test(e2e_basic.short_circuit short_circuit.ms)
mslangc_add_e2e_basic_test(e2e_basic.loops loops.ms)
mslangc_add_e2e_named_test(functions.recursion
  "e2e/functions/recursion.ms"
  "e2e/functions/recursion.ms.out"
  "functions")
mslangc_add_e2e_named_test(functions.function_expr
  "e2e/functions/function_expr.ms"
  "e2e/functions/function_expr.ms.out"
  "functions")
mslangc_add_e2e_named_test(closures.capture
  "e2e/closures/capture.ms"
  "e2e/closures/capture.ms.out"
  "closures")
mslangc_add_e2e_named_test(closures.nested
  "e2e/closures/nested.ms"
  "e2e/closures/nested.ms.out"
  "closures")
mslangc_add_cli_test(containers.truthiness
  -DMSLANGC_EXE="$<TARGET_FILE:containers_e2e_runner>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/truthiness.ms"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/truthiness.ms.out"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.truthiness PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.length
  -DMSLANGC_EXE="$<TARGET_FILE:containers_e2e_runner>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/length.ms"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/length.ms.out"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.length PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.literals
  -DMSLANGC_EXE="$<TARGET_FILE:containers_e2e_runner>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/literals.ms"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/literals.ms.out"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.literals PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.list_indexing
  -DMSLANGC_EXE="$<TARGET_FILE:containers_e2e_runner>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/list_indexing.ms"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/list_indexing.ms.out"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.list_indexing PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.tuple_index_read
  -DMSLANGC_EXE="$<TARGET_FILE:containers_e2e_runner>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/tuple_index_read.ms"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/tuple_index_read.ms.out"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.tuple_index_read PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.map_indexing
  -DMSLANGC_EXE="$<TARGET_FILE:containers_e2e_runner>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/map_indexing.ms"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/map_indexing.ms.out"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.map_indexing PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.regression_chain
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/regression_chain.ms"
  -DEXPECT_EXIT=0
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/regression_chain.ms.out"
  -DEXPECT_STDERR=""
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.regression_chain PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.invalid_index_target
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/invalid_index_target.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;code=MS4012;value is not indexable"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.invalid_index_target PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.non_integral_index
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/non_integral_index.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;code=MS4013;index must be a non-negative integer"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.non_integral_index PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.index_out_of_range
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/index_out_of_range.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;code=MS4014;index out of range"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.index_out_of_range PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.tuple_write
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/tuple_write.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;code=MS4015;tuple elements are immutable"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.tuple_write PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.missing_map_key
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/missing_map_key.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;code=MS4016;map key not found"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.missing_map_key PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.string_indexing
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/string_indexing.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;code=MS4012;value is not indexable"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.string_indexing PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")
mslangc_add_cli_test(containers.non_string_map_key
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/containers/non_string_map_key.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;code=MS4011;map keys must be strings"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(containers.non_string_map_key PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};containers")

mslangc_add_e2e_named_test(class.basic_class
  "e2e/class/basic_class.ms"
  "e2e/class/basic_class.ms.out"
  "class")
mslangc_add_e2e_named_test(class.fields_and_methods
  "e2e/class/fields_and_methods.ms"
  "e2e/class/fields_and_methods.ms.out"
  "class")
mslangc_add_e2e_named_test(class.field_shadows_method
  "e2e/class/field_shadows_method.ms"
  "e2e/class/field_shadows_method.ms.out"
  "class")
mslangc_add_e2e_named_test(class.bound_method
  "e2e/class/bound_method.ms"
  "e2e/class/bound_method.ms.out"
  "class")
mslangc_add_e2e_named_test(class.init_returns_instance
  "e2e/class/init_returns_instance.ms"
  "e2e/class/init_returns_instance.ms.out"
  "class")
mslangc_add_e2e_named_test(class.inheritance_override
  "e2e/class/inheritance_override.ms"
  "e2e/class/inheritance_override.ms.out"
  "class")
mslangc_add_e2e_named_test(class.super_dispatch
  "e2e/class/super_dispatch.ms"
  "e2e/class/super_dispatch.ms.out"
  "class")
mslangc_add_e2e_named_test(modules.import_basic
  "e2e/modules/import_basic.ms"
  "e2e/modules/import_basic.ms.out"
  "modules")
mslangc_add_e2e_named_test(modules.import_alias
  "e2e/modules/import_alias.ms"
  "e2e/modules/import_alias.ms.out"
  "modules")
mslangc_add_e2e_named_test(modules.from_import
  "e2e/modules/from_import.ms"
  "e2e/modules/from_import.ms.out"
  "modules")
mslangc_add_e2e_named_test(modules.from_import_alias
  "e2e/modules/from_import_alias.ms"
  "e2e/modules/from_import_alias.ms.out"
  "modules")
mslangc_add_e2e_named_test(modules.from_import_snapshot
  "e2e/modules/from_import_snapshot.ms"
  "e2e/modules/from_import_snapshot.ms.out"
  "modules")
mslangc_add_cli_test(modules.missing_export
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/missing_export.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/missing_export.ms.out"
  -DEXPECT_STDERR_CONTAINS="phase=module;code=MS5002;missing_export.ms:4:"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(modules.missing_export PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${MSLANGC_TEST_LABEL_CONFORMANCE};modules")
mslangc_add_e2e_named_test(modules.cache_once
  "e2e/modules/cache_once.ms"
  "e2e/modules/cache_once.ms.out"
  "modules")
mslangc_add_e2e_named_test(modules.shared_dependency_once
  "e2e/modules/shared_dependency_once.ms"
  "e2e/modules/shared_dependency_once.ms.out"
  "modules")
mslangc_add_e2e_named_test(modules.namespace_isolation
  "e2e/modules/namespace_isolation.ms"
  "e2e/modules/namespace_isolation.ms.out"
  "modules")
mslangc_add_cli_test(modules.missing_module
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/missing_module.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=module;code=MS5001;missing_module.ms:1:"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(modules.missing_module PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};modules")
mslangc_add_cli_test(modules.cyclic_dependency
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/cyclic_dependency.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=module;code=MS5003;cyclic_dependency.ms:1:1;phase=module;code=MS5004;cyclic_dependency.ms:1:1"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(modules.cyclic_dependency PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};modules")
mslangc_add_cli_test(modules.import_parse_failure
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/import_parse_failure.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=module;code=MS5004;import_parse_failure.ms:1:1;phase=parse;code=MS2001;parse_fail.ms:2:1"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(modules.import_parse_failure PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};modules")
mslangc_add_cli_test(modules.import_resolve_failure
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/import_resolve_failure.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=module;code=MS5004;import_resolve_failure.ms:1:1;phase=resolve;code=MS3003;resolve_fail.ms:1:1"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(modules.import_resolve_failure PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};modules")
mslangc_add_cli_test(modules.import_runtime_failure
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/import_runtime_failure.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=module;code=MS5004;import_runtime_failure.ms:1:1;phase=runtime;code=MS4004;runtime_fail.ms:1:1"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(modules.import_runtime_failure PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};modules")
mslangc_add_e2e_named_test(modules.regression_chain
  "e2e/modules/regression_chain.ms"
  "e2e/modules/regression_chain.ms.out"
  "modules")
mslangc_add_e2e_named_test(modules.temp_root_success
  "e2e/modules/temp_root_success.ms"
  "e2e/modules/temp_root_success.ms.out"
  "modules")
mslangc_add_cli_test(modules.temp_root_failure
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/modules/temp_root_failure.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=module;code=MS5004;temp_root_failure.ms:1:1;phase=runtime;code=MS4004;runtime_fail.ms:1:1"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
set_tests_properties(modules.temp_root_failure PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};modules")
mslangc_add_cli_test(functions.non_callable
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/functions/non_callable.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;MS4006"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
mslangc_add_cli_test(functions.wrong_arity
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/functions/wrong_arity.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=runtime;MS4007"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
mslangc_add_cli_test(resolver_static.top_level_return
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/top_level_return.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=resolve;MS3001"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.duplicate_module_binding
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/duplicate_module_binding.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=resolve;MS3002"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.duplicate_import_alias_binding
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/duplicate_import_alias_binding.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=resolve;MS3002"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.duplicate_local_binding
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/duplicate_local_binding.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=resolve;MS3002"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.self_read_initializer
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/self_read_initializer.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=resolve;MS3004"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
mslangc_add_cli_test(resolver_static.illegal_self
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/illegal_self_outside_class.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="code=MS3007"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.illegal_super
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/illegal_super_outside_subclass.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="code=MS3008"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.illegal_init_return
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/illegal_init_return_value.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="code=MS3009"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.self_inherit
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/class_inherits_from_itself.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="code=MS3010"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.illegal_break
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/illegal_break.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=resolve;MS3003"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

mslangc_add_cli_test(resolver_static.illegal_continue
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/ms/resolver/illegal_continue.ms"
  -DEXPECT_EXIT=66
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="phase=resolve;MS3003"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
mslangc_add_cli_test(class.missing_property
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/class/missing_property.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="code=MS4010"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")
mslangc_add_cli_test(class.invalid_property_receiver
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -DSCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/e2e/class/invalid_property_receiver.ms"
  -DEXPECT_EXIT=70
  -DEXPECT_STDOUT=""
  -DEXPECT_STDERR_CONTAINS="code=MS4009"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_mslangc_run.cmake")

add_test(NAME gc.stress
  COMMAND "C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    -NoLogo
    -NoProfile
    -ExecutionPolicy
    Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/run_gc_stress.ps1"
    -Runner "$<TARGET_FILE:mslangc>"
    -Root "${CMAKE_CURRENT_SOURCE_DIR}/stress/gc")
set_tests_properties(gc.stress PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};gc;stress")

add_test(NAME gc.modules
  COMMAND "C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    -NoLogo
    -NoProfile
    -ExecutionPolicy
    Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/run_gc_modules.ps1"
    -CTest "${CMAKE_CTEST_COMMAND}"
    -BuildDir "${CMAKE_BINARY_DIR}")
set_tests_properties(gc.modules PROPERTIES
  LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};gc;modules")
