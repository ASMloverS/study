mslangc_add_smoke_test(smoke.help
  -DMSLANGC_EXE="$<TARGET_FILE:mslangc>"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/smoke_help_test.cmake")

function(mslangc_add_cli_test test_name)
  add_test(NAME "${test_name}"
    COMMAND "${CMAKE_COMMAND}" ${ARGN})
  set_tests_properties("${test_name}" PROPERTIES
    LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};runner")
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
    LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};e2e_basic")
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
    LABELS "${MSLANGC_TEST_LABEL_INTEGRATION};${test_label}")
endfunction()

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