# run_fixture.cmake -- run a .ms fixture and compare to // expect: comments.
# Usage: cmake -DEXE=<path> -DSCRIPT=<path.ms> -P run_fixture.cmake

cmake_minimum_required(VERSION 3.20)

file(READ "${SCRIPT}" content)

# Extract expected lines
string(REGEX MATCHALL "// expect: [^\n]*" raw_expects "${content}")
set(expected_lines "")
foreach(m IN LISTS raw_expects)
    string(REGEX REPLACE "// expect: (.*)" "\\1" line "${m}")
    string(STRIP "${line}" line)
    list(APPEND expected_lines "${line}")
endforeach()

# Run the interpreter
execute_process(
    COMMAND "${EXE}" "${SCRIPT}"
    OUTPUT_VARIABLE actual_out
    ERROR_VARIABLE  err_out
    RESULT_VARIABLE rc
    TIMEOUT 15
)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Exit ${rc}: ${err_out}")
endif()

# Normalise CRLF
string(REGEX REPLACE "\r\n" "\n" actual_out "${actual_out}")
string(REGEX REPLACE "\n$" "" actual_out "${actual_out}")

# Split actual output into lines
string(REGEX MATCHALL "[^\n]+" actual_lines "${actual_out}")

list(LENGTH expected_lines exp_len)
list(LENGTH actual_lines   act_len)

if(NOT exp_len EQUAL act_len)
    string(JOIN "\n" exp_str ${expected_lines})
    message(FATAL_ERROR
        "Line count: expected ${exp_len}, got ${act_len}\n"
        "Expected:\n${exp_str}\n"
        "Actual:\n${actual_out}")
endif()

if(exp_len GREATER 0)
    math(EXPR last "${exp_len} - 1")
    foreach(i RANGE ${last})
        list(GET expected_lines ${i} exp_line)
        list(GET actual_lines   ${i} act_line)
        string(STRIP "${act_line}" act_line)
        if(NOT exp_line STREQUAL act_line)
            message(FATAL_ERROR "Line ${i}: expected '${exp_line}' got '${act_line}'")
        endif()
    endforeach()
endif()
