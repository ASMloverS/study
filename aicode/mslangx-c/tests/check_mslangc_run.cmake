function(mslangc_unquote out_var input_value)
  string(REGEX REPLACE "^\"|\"$" "" cleaned "${input_value}")
  set(${out_var} "${cleaned}" PARENT_SCOPE)
endfunction()

mslangc_unquote(actual_mslangc_exe "${MSLANGC_EXE}")
if(NOT DEFINED MSLANGC_EXE OR NOT EXISTS "${actual_mslangc_exe}")
  message(FATAL_ERROR "mslangc executable not found: ${actual_mslangc_exe}")
endif()

if(DEFINED SCRIPT AND DEFINED INLINE_SOURCE)
  message(FATAL_ERROR "Specify either SCRIPT or INLINE_SOURCE, not both")
endif()

if(NOT DEFINED SCRIPT AND NOT DEFINED INLINE_SOURCE)
  message(FATAL_ERROR "Either SCRIPT or INLINE_SOURCE is required")
endif()

set(command_args)
if(DEFINED INLINE_SOURCE)
  mslangc_unquote(actual_inline_source "${INLINE_SOURCE}")
  list(APPEND command_args -e "${actual_inline_source}")
else()
  mslangc_unquote(actual_script "${SCRIPT}")
  if(NOT EXISTS "${actual_script}")
    message(FATAL_ERROR "Script not found: ${actual_script}")
  endif()
  list(APPEND command_args "${actual_script}")
endif()

execute_process(
  COMMAND "${actual_mslangc_exe}" ${command_args}
  RESULT_VARIABLE actual_exit
  OUTPUT_VARIABLE actual_stdout
  ERROR_VARIABLE actual_stderr)

if(NOT "${actual_exit}" STREQUAL "${EXPECT_EXIT}")
  message(FATAL_ERROR
    "Unexpected exit code.\nExpected: ${EXPECT_EXIT}\nActual: ${actual_exit}\nstdout:\n${actual_stdout}\nstderr:\n${actual_stderr}")
endif()

if(DEFINED EXPECT_STDOUT_FILE)
  mslangc_unquote(actual_stdout_file "${EXPECT_STDOUT_FILE}")
  file(READ "${actual_stdout_file}" expected_stdout)
else()
  mslangc_unquote(actual_expect_stdout "${EXPECT_STDOUT}")
  string(REPLACE "\\n" "\n" expected_stdout "${actual_expect_stdout}")
endif()

if(NOT "${actual_stdout}" STREQUAL "${expected_stdout}")
  message(FATAL_ERROR
    "Unexpected stdout.\nExpected:\n${expected_stdout}\nActual:\n${actual_stdout}")
endif()

if(DEFINED EXPECT_STDERR_CONTAINS)
  foreach(expected_fragment IN LISTS EXPECT_STDERR_CONTAINS)
    mslangc_unquote(actual_fragment "${expected_fragment}")
    string(FIND "${actual_stderr}" "${actual_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
      message(FATAL_ERROR
        "stderr missing fragment '${actual_fragment}'.\nstderr:\n${actual_stderr}")
    endif()
  endforeach()
elseif(DEFINED EXPECT_STDERR)
  mslangc_unquote(actual_expect_stderr "${EXPECT_STDERR}")
  string(REPLACE "\\n" "\n" expected_stderr "${actual_expect_stderr}")
  if(NOT "${actual_stderr}" STREQUAL "${expected_stderr}")
    message(FATAL_ERROR
      "Unexpected stderr.\nExpected:\n${expected_stderr}\nActual:\n${actual_stderr}")
  endif()
endif()