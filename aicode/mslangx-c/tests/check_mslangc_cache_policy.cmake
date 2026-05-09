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
  set(one_value_args EXPECT_EXIT EXPECT_STDOUT EXPECT_STDOUT_FILE EXPECT_STDERR EXPECT_STDERR_CONTAINS)
  set(multi_value_args COMMAND)
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

  if(DEFINED RUN_EXPECT_STDOUT_FILE)
    mslangc_unquote(actual_stdout_file "${RUN_EXPECT_STDOUT_FILE}")
    file(READ "${actual_stdout_file}" expected_stdout)
    mslangc_normalize_newlines(expected_stdout "${expected_stdout}")
  else()
    mslangc_unquote(actual_expect_stdout "${RUN_EXPECT_STDOUT}")
    string(REPLACE "\\n" "\n" expected_stdout "${actual_expect_stdout}")
    mslangc_normalize_newlines(expected_stdout "${expected_stdout}")
  endif()

  if(NOT "${actual_stdout}" STREQUAL "${expected_stdout}")
    message(FATAL_ERROR
      "Unexpected stdout.\nExpected:\n${expected_stdout}\nActual:\n${actual_stdout}")
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

function(mslangc_file_timestamp out_var path)
  file(TIMESTAMP "${path}" timestamp "%Y-%m-%dT%H:%M:%S" UTC)
  set(${out_var} "${timestamp}" PARENT_SCOPE)
endfunction()

function(mslangc_expect_cache_file cache_path)
  if(NOT EXISTS "${cache_path}")
    message(FATAL_ERROR "Expected cache file: ${cache_path}")
  endif()
endfunction()

function(mslangc_replace_script_from_fixture script_path fixture_path)
  mslangc_unquote(actual_fixture_path "${fixture_path}")
  if(NOT EXISTS "${actual_fixture_path}")
    message(FATAL_ERROR "Replacement fixture not found: ${actual_fixture_path}")
  endif()
  file(READ "${actual_fixture_path}" replacement_source)
  file(WRITE "${script_path}" "${replacement_source}")
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

mslangc_unquote(actual_mslangc_exe "${MSLANGC_EXE}")
mslangc_unquote(actual_work_dir "${WORK_DIR}")

if(NOT EXISTS "${actual_mslangc_exe}")
  message(FATAL_ERROR "mslangc executable not found: ${actual_mslangc_exe}")
endif()

file(REMOVE_RECURSE "${actual_work_dir}")
file(MAKE_DIRECTORY "${actual_work_dir}")

if(DEFINED SOURCE_FIXTURE)
  mslangc_unquote(actual_source_fixture "${SOURCE_FIXTURE}")
  if(NOT EXISTS "${actual_source_fixture}")
    message(FATAL_ERROR "Source fixture not found: ${actual_source_fixture}")
  endif()
  get_filename_component(source_fixture_name "${actual_source_fixture}" NAME)
  file(COPY "${actual_source_fixture}" DESTINATION "${actual_work_dir}")
  set(script_path "${actual_work_dir}/${source_fixture_name}")
  get_filename_component(script_name_we "${source_fixture_name}" NAME_WE)
  set(cache_path "${actual_work_dir}/__mscache__/${script_name_we}.msc")
endif()

if(MODE STREQUAL "create_reuse")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  mslangc_expect_cache_file("${cache_path}")
  mslangc_file_timestamp(cache_mtime_before "${cache_path}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  mslangc_expect_cache_file("${cache_path}")
  mslangc_file_timestamp(cache_mtime_after "${cache_path}")
  if(NOT "${cache_mtime_before}" STREQUAL "${cache_mtime_after}")
    message(FATAL_ERROR
      "Cache file should not be rewritten when a valid cache is reused.\nBefore: ${cache_mtime_before}\nAfter: ${cache_mtime_after}\nPath: ${cache_path}")
  endif()
elseif(MODE STREQUAL "corrupt_fallback")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  mslangc_expect_cache_file("${cache_path}")
  file(WRITE "${cache_path}" "corrupt-cache")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
elseif(MODE STREQUAL "stale_fallback")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  mslangc_expect_cache_file("${cache_path}")
  mslangc_replace_script_from_fixture("${script_path}" "${REPLACEMENT_FIXTURE}")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STALE_STDOUT_FILE}")
elseif(MODE STREQUAL "no_cache")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  mslangc_expect_cache_file("${cache_path}")
  mslangc_file_timestamp(cache_mtime_before "${cache_path}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
  mslangc_replace_script_from_fixture("${script_path}" "${REPLACEMENT_FIXTURE}")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" --no-cache "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STALE_STDOUT_FILE}")
  mslangc_expect_cache_file("${cache_path}")
  mslangc_file_timestamp(cache_mtime_after "${cache_path}")
  if(NOT "${cache_mtime_before}" STREQUAL "${cache_mtime_after}")
    message(FATAL_ERROR
      "Cache file should not be rewritten when --no-cache is used.\nBefore: ${cache_mtime_before}\nAfter: ${cache_mtime_after}\nPath: ${cache_path}")
  endif()
elseif(MODE STREQUAL "inline_no_cache")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" -e "print 7"
    EXPECT_EXIT 0
    EXPECT_STDOUT "7\n")
  if(EXISTS "${actual_work_dir}/__mscache__")
    message(FATAL_ERROR
      "Inline execution should not create a disk cache: ${actual_work_dir}/__mscache__")
  endif()
elseif(MODE STREQUAL "unknown_option")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" --bogus
    EXPECT_EXIT 64
    EXPECT_STDERR "error: unknown option: --bogus\n")
else()
  message(FATAL_ERROR "Unknown MODE: ${MODE}")
endif()
