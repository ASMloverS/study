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