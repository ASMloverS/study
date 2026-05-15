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

function(mslangc_patch_u16_le path offset value)
  execute_process(
    COMMAND C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
      -NoProfile
      -Command
      "$p = '${path}'
$b = [System.IO.File]::ReadAllBytes($p)
$o = [int]${offset}
$v = [uint16]${value}
$x = [BitConverter]::GetBytes($v)
[Array]::Copy($x, 0, $b, $o, 2)
[System.IO.File]::WriteAllBytes($p, $b)"
    RESULT_VARIABLE patch_exit
    OUTPUT_VARIABLE patch_stdout
    ERROR_VARIABLE patch_stderr)

  if(NOT patch_exit EQUAL 0)
    message(FATAL_ERROR
      "Failed to patch cache file.\nExit: ${patch_exit}\nstdout:\n${patch_stdout}\nstderr:\n${patch_stderr}")
  endif()
endfunction()

function(mslangc_copy_source_fixture work_dir source_fixture out_script_path out_cache_path)
  get_filename_component(source_name "${source_fixture}" NAME)
  get_filename_component(source_name_we "${source_fixture}" NAME_WE)
  file(COPY "${source_fixture}" DESTINATION "${work_dir}")
  set(${out_script_path} "${work_dir}/${source_name}" PARENT_SCOPE)
  set(${out_cache_path} "${work_dir}/__mscache__/${source_name_we}.msc" PARENT_SCOPE)
endfunction()

function(mslangc_copy_module_fixture_root work_dir module_fixture_root)
  file(COPY "${module_fixture_root}" DESTINATION "${work_dir}/tests/fixtures")
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
  mslangc_copy_source_fixture("${actual_work_dir}"
    "${actual_source_fixture}"
    script_path
    cache_path)
endif()

if(MODE STREQUAL "direct_import_source_free" OR
   MODE STREQUAL "direct_import_fallback_source")
  if(NOT DEFINED MODULE_FIXTURE_ROOT)
    message(FATAL_ERROR "MODULE_FIXTURE_ROOT is required")
  endif()
  mslangc_unquote(actual_module_fixture_root "${MODULE_FIXTURE_ROOT}")
  if(NOT EXISTS "${actual_module_fixture_root}")
    message(FATAL_ERROR "Module fixture root not found: ${actual_module_fixture_root}")
  endif()
endif()

if(MODE STREQUAL "direct_import_source_free" OR
   MODE STREQUAL "direct_import_fallback_source")
  if(NOT DEFINED MODULE_SOURCE_FIXTURE)
    message(FATAL_ERROR "MODULE_SOURCE_FIXTURE is required")
  endif()
  mslangc_unquote(actual_module_source_fixture "${MODULE_SOURCE_FIXTURE}")
  if(NOT EXISTS "${actual_module_source_fixture}")
    message(FATAL_ERROR "Module source fixture not found: ${actual_module_source_fixture}")
  endif()

  file(RELATIVE_PATH module_relative_path
    "${actual_module_fixture_root}"
    "${actual_module_source_fixture}")
  get_filename_component(module_relative_dir "${module_relative_path}" DIRECTORY)
  get_filename_component(module_source_name_we "${actual_module_source_fixture}" NAME_WE)
  set(module_source_path
    "${actual_work_dir}/tests/fixtures/modules/${module_relative_path}")
  set(module_cache_path
    "${actual_work_dir}/tests/fixtures/modules/${module_relative_dir}/__mscache__/${module_source_name_we}.msc")
endif()

if(MODE STREQUAL "direct_create_reuse")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  file(REMOVE "${script_path}")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${cache_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
elseif(MODE STREQUAL "direct_runtime_error")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 70
    EXPECT_STDOUT ""
    EXPECT_STDERR_CONTAINS "${EXPECT_STDERR_CONTAINS}")
  file(REMOVE "${script_path}")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${cache_path}"
    EXPECT_EXIT 70
    EXPECT_STDOUT ""
    EXPECT_STDERR_CONTAINS "${EXPECT_STDERR_CONTAINS}")
elseif(MODE STREQUAL "direct_no_cache")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  file(REMOVE "${script_path}")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" --no-cache "${cache_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
elseif(MODE STREQUAL "direct_corrupt")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  file(REMOVE "${script_path}")
  file(WRITE "${cache_path}" "corrupt-cache")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${cache_path}"
    EXPECT_EXIT 74
    EXPECT_STDOUT ""
    EXPECT_STDERR_CONTAINS "error: corrupt cache file: ${cache_path}")
elseif(MODE STREQUAL "direct_incompatible_version")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  file(REMOVE "${script_path}")
  mslangc_patch_u16_le("${cache_path}" 8 65535)
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${cache_path}"
    EXPECT_EXIT 74
    EXPECT_STDOUT ""
    EXPECT_STDERR_CONTAINS "error: incompatible cache file: ${cache_path}")
elseif(MODE STREQUAL "direct_incompatible_abi")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  file(REMOVE "${script_path}")
  mslangc_patch_u16_le("${cache_path}" 10 65535)
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${cache_path}"
    EXPECT_EXIT 74
    EXPECT_STDOUT ""
    EXPECT_STDERR_CONTAINS "error: incompatible cache file: ${cache_path}")
elseif(MODE STREQUAL "direct_import_moved_cache")
  if(NOT DEFINED MODULE_FIXTURE_ROOT)
    message(FATAL_ERROR "MODULE_FIXTURE_ROOT is required")
  endif()
  mslangc_unquote(actual_module_fixture_root "${MODULE_FIXTURE_ROOT}")
  if(NOT EXISTS "${actual_module_fixture_root}")
    message(FATAL_ERROR "Module fixture root not found: ${actual_module_fixture_root}")
  endif()

  file(MAKE_DIRECTORY "${actual_work_dir}/tests/e2e/mscache")
  mslangc_copy_source_fixture("${actual_work_dir}/tests/e2e/mscache"
    "${actual_source_fixture}"
    script_path
    cache_path)
  mslangc_copy_module_fixture_root("${actual_work_dir}"
    "${actual_module_fixture_root}")
  get_filename_component(script_name_we "${script_path}" NAME_WE)
  set(relocated_cache_dir "${actual_work_dir}/relocated")
  set(relocated_cache_path "${relocated_cache_dir}/${script_name_we}.msc")

  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  file(MAKE_DIRECTORY "${relocated_cache_dir}")
  file(RENAME "${cache_path}" "${relocated_cache_path}")
  file(REMOVE "${script_path}")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${relocated_cache_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
elseif(MODE STREQUAL "direct_import_source_free")
  if(NOT DEFINED MODULE_FIXTURE_ROOT)
    message(FATAL_ERROR "MODULE_FIXTURE_ROOT is required")
  endif()
  mslangc_unquote(actual_module_fixture_root "${MODULE_FIXTURE_ROOT}")
  if(NOT EXISTS "${actual_module_fixture_root}")
    message(FATAL_ERROR "Module fixture root not found: ${actual_module_fixture_root}")
  endif()

  file(MAKE_DIRECTORY "${actual_work_dir}/tests/e2e/mscache")
  mslangc_copy_source_fixture("${actual_work_dir}/tests/e2e/mscache"
    "${actual_source_fixture}"
    script_path
    cache_path)
  mslangc_copy_module_fixture_root("${actual_work_dir}"
    "${actual_module_fixture_root}")

  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  file(REMOVE "${script_path}")
  file(REMOVE "${module_source_path}")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${cache_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
elseif(MODE STREQUAL "direct_import_fallback_source")
  if(NOT DEFINED MODULE_FIXTURE_ROOT)
    message(FATAL_ERROR "MODULE_FIXTURE_ROOT is required")
  endif()
  mslangc_unquote(actual_module_fixture_root "${MODULE_FIXTURE_ROOT}")
  if(NOT EXISTS "${actual_module_fixture_root}")
    message(FATAL_ERROR "Module fixture root not found: ${actual_module_fixture_root}")
  endif()

  file(MAKE_DIRECTORY "${actual_work_dir}/tests/e2e/mscache")
  mslangc_copy_source_fixture("${actual_work_dir}/tests/e2e/mscache"
    "${actual_source_fixture}"
    script_path
    cache_path)
  mslangc_copy_module_fixture_root("${actual_work_dir}"
    "${actual_module_fixture_root}")

  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${script_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
  file(REMOVE "${script_path}")
  file(REMOVE "${module_cache_path}")
  mslangc_expect_run(
    COMMAND "${actual_mslangc_exe}" "${cache_path}"
    EXPECT_EXIT 0
    EXPECT_STDOUT_FILE "${EXPECT_STDOUT_FILE}")
else()
  message(FATAL_ERROR "Unknown MODE: ${MODE}")
endif()
