function(mslangc_unquote out_var input_value)
  string(REGEX REPLACE "^\"|\"$" "" cleaned "${input_value}")
  set(${out_var} "${cleaned}" PARENT_SCOPE)
endfunction()

function(mslangc_normalize_newlines out_var input_value)
  set(normalized "${input_value}")
  string(REPLACE "\r\n" "\n" normalized "${normalized}")
  string(REPLACE "\r" "\n" normalized "${normalized}")
  set(${out_var} "${normalized}" PARENT_SCOPE)
endfunction()

function(mslangc_expect_run)
  set(one_value_args EXPECT_EXIT EXPECT_STDOUT EXPECT_STDOUT_FILE EXPECT_STDERR)
  set(multi_value_args COMMAND EXPECT_STDERR_CONTAINS)
  cmake_parse_arguments(RUN "" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT DEFINED RUN_EXPECT_EXIT)
    message(FATAL_ERROR "EXPECT_EXIT is required")
  endif()
  if(NOT RUN_COMMAND)
    message(FATAL_ERROR "COMMAND is required")
  endif()

  execute_process(
    COMMAND ${RUN_COMMAND}
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr)

  if(NOT "${actual_exit}" STREQUAL "${RUN_EXPECT_EXIT}")
    message(FATAL_ERROR
      "Unexpected exit code.\nExpected: ${RUN_EXPECT_EXIT}\nActual: ${actual_exit}\nstdout:\n${actual_stdout}\nstderr:\n${actual_stderr}")
  endif()

  mslangc_normalize_newlines(actual_stdout "${actual_stdout}")
  mslangc_normalize_newlines(actual_stderr "${actual_stderr}")

  set(has_stdout_expectation 0)
  if(DEFINED RUN_EXPECT_STDOUT_FILE)
    mslangc_unquote(actual_stdout_file "${RUN_EXPECT_STDOUT_FILE}")
    file(READ "${actual_stdout_file}" expected_stdout)
    mslangc_normalize_newlines(expected_stdout "${expected_stdout}")
    set(has_stdout_expectation 1)
  elseif(DEFINED RUN_EXPECT_STDOUT)
    mslangc_unquote(actual_expect_stdout "${RUN_EXPECT_STDOUT}")
    string(REPLACE "\\n" "\n" expected_stdout "${actual_expect_stdout}")
    mslangc_normalize_newlines(expected_stdout "${expected_stdout}")
    set(has_stdout_expectation 1)
  endif()

  if(has_stdout_expectation)
    if(NOT "${actual_stdout}" STREQUAL "${expected_stdout}")
      message(FATAL_ERROR
        "Unexpected stdout.\nExpected:\n${expected_stdout}\nActual:\n${actual_stdout}")
    endif()
  endif()

  if(DEFINED RUN_EXPECT_STDERR_CONTAINS)
    foreach(expected_fragment IN LISTS RUN_EXPECT_STDERR_CONTAINS)
      mslangc_unquote(actual_fragment "${expected_fragment}")
      mslangc_normalize_newlines(actual_fragment "${actual_fragment}")
      string(FIND "${actual_stderr}" "${actual_fragment}" fragment_index)
      if(fragment_index EQUAL -1)
        message(FATAL_ERROR
          "stderr missing fragment '${actual_fragment}'.\nstderr:\n${actual_stderr}")
      endif()
    endforeach()
  elseif(DEFINED RUN_EXPECT_STDERR)
    mslangc_unquote(actual_expect_stderr "${RUN_EXPECT_STDERR}")
    string(REPLACE "\\n" "\n" expected_stderr "${actual_expect_stderr}")
    mslangc_normalize_newlines(expected_stderr "${expected_stderr}")
    if(NOT "${actual_stderr}" STREQUAL "${expected_stderr}")
      message(FATAL_ERROR
        "Unexpected stderr.\nExpected:\n${expected_stderr}\nActual:\n${actual_stderr}")
    endif()
  endif()
endfunction()

function(mslangc_expect_cache_file cache_path)
  if(NOT EXISTS "${cache_path}")
    message(FATAL_ERROR "Expected cache file: ${cache_path}")
  endif()
endfunction()

function(mslangc_expect_missing_cache_file cache_path)
  if(EXISTS "${cache_path}")
    message(FATAL_ERROR "Expected no cache file: ${cache_path}")
  endif()
endfunction()

function(mslangc_file_timestamp out_var path)
  file(TIMESTAMP "${path}" timestamp "%Y-%m-%dT%H:%M:%S" UTC)
  set(${out_var} "${timestamp}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED MSLANGC_EXE)
  message(FATAL_ERROR "MSLANGC_EXE is not set")
endif()
if(NOT DEFINED MODE)
  message(FATAL_ERROR "MODE is required")
endif()
if(NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is required")
endif()
if(NOT DEFINED SOURCE_FIXTURE)
  message(FATAL_ERROR "SOURCE_FIXTURE is required")
endif()
if(NOT DEFINED MODULE_FIXTURE_ROOT)
  message(FATAL_ERROR "MODULE_FIXTURE_ROOT is required")
endif()
if(NOT DEFINED MODULE_SOURCE_FIXTURE)
  message(FATAL_ERROR "MODULE_SOURCE_FIXTURE is required")
endif()

mslangc_unquote(actual_mslangc_exe "${MSLANGC_EXE}")
mslangc_unquote(actual_work_dir "${WORK_DIR}")
mslangc_unquote(actual_source_fixture "${SOURCE_FIXTURE}")
mslangc_unquote(actual_module_fixture_root "${MODULE_FIXTURE_ROOT}")
mslangc_unquote(actual_module_source_fixture "${MODULE_SOURCE_FIXTURE}")

if(NOT EXISTS "${actual_mslangc_exe}")
  message(FATAL_ERROR "mslangc executable not found: ${actual_mslangc_exe}")
endif()
if(NOT EXISTS "${actual_source_fixture}")
  message(FATAL_ERROR "Source fixture not found: ${actual_source_fixture}")
endif()
if(NOT EXISTS "${actual_module_fixture_root}")
  message(FATAL_ERROR "Module fixture root not found: ${actual_module_fixture_root}")
endif()
if(NOT EXISTS "${actual_module_source_fixture}")
  message(FATAL_ERROR "Module source fixture not found: ${actual_module_source_fixture}")
endif()

file(REMOVE_RECURSE "${actual_work_dir}")
file(MAKE_DIRECTORY "${actual_work_dir}")

get_filename_component(source_fixture_name "${actual_source_fixture}" NAME)
get_filename_component(source_fixture_name_we "${actual_source_fixture}" NAME_WE)
file(COPY "${actual_source_fixture}" DESTINATION "${actual_work_dir}")
file(COPY "${actual_module_fixture_root}" DESTINATION "${actual_work_dir}/fixtures")

file(GLOB_RECURSE copied_fixture_paths LIST_DIRECTORIES true
  "${actual_work_dir}/fixtures/modules/*")
foreach(copied_fixture_path IN LISTS copied_fixture_paths)
  get_filename_component(copied_fixture_name "${copied_fixture_path}" NAME)
  if(copied_fixture_name STREQUAL "__mscache__")
    file(REMOVE_RECURSE "${copied_fixture_path}")
  endif()
endforeach()

set(script_path "${actual_work_dir}/${source_fixture_name}")
set(entry_cache_path "${actual_work_dir}/__mscache__/${source_fixture_name_we}.msc")

file(RELATIVE_PATH module_relative_path
  "${actual_module_fixture_root}"
  "${actual_module_source_fixture}")
get_filename_component(module_relative_dir "${module_relative_path}" DIRECTORY)
get_filename_component(module_source_name_we "${actual_module_source_fixture}" NAME_WE)
set(module_cache_path
  "${actual_work_dir}/fixtures/modules/${module_relative_dir}/__mscache__/${module_source_name_we}.msc")

if(MODE STREQUAL "create_reuse")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  mslangc_expect_cache_file("${entry_cache_path}")
  mslangc_expect_cache_file("${module_cache_path}")
  mslangc_file_timestamp(entry_cache_before "${entry_cache_path}")
  mslangc_file_timestamp(module_cache_before "${module_cache_path}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  mslangc_expect_cache_file("${entry_cache_path}")
  mslangc_expect_cache_file("${module_cache_path}")
  mslangc_file_timestamp(entry_cache_after "${entry_cache_path}")
  mslangc_file_timestamp(module_cache_after "${module_cache_path}")
  if(NOT "${entry_cache_before}" STREQUAL "${entry_cache_after}")
    message(FATAL_ERROR
      "Entry cache file should not be rewritten when a valid cache is reused.\nBefore: ${entry_cache_before}\nAfter: ${entry_cache_after}\nPath: ${entry_cache_path}")
  endif()
  if(NOT "${module_cache_before}" STREQUAL "${module_cache_after}")
    message(FATAL_ERROR
      "Module cache file should not be rewritten when a valid cache is reused.\nBefore: ${module_cache_before}\nAfter: ${module_cache_after}\nPath: ${module_cache_path}")
  endif()
elseif(MODE STREQUAL "no_cache")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" --no-cache "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  mslangc_expect_missing_cache_file("${entry_cache_path}")
  mslangc_expect_missing_cache_file("${module_cache_path}")
elseif(MODE STREQUAL "cycle_failure")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 70
    EXPECT_STDOUT ""
    EXPECT_STDERR_CONTAINS ${EXPECT_STDERR_CONTAINS})
elseif(MODE STREQUAL "runtime_failure")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 70
    EXPECT_STDOUT ""
    EXPECT_STDERR_CONTAINS ${EXPECT_STDERR_CONTAINS})
else()
  message(FATAL_ERROR "Unknown MODE: ${MODE}")
endif()
