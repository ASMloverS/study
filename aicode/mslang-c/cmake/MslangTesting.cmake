function(ms_add_test name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/tests
    )
    set(_opts ${MSLANG_COMPILE_OPTIONS})
    if(MSVC)
        list(APPEND _opts /wd4127)
    endif()
    target_compile_options(${name} PRIVATE ${_opts})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ms_add_fixture_test(script_path [TIMEOUT secs])
# Runs mslang-c on script_path and checks output against // expect: comments.
function(ms_add_fixture_test script_path)
    cmake_parse_arguments(_ft "" "TIMEOUT" "" ${ARGN})
    if(NOT _ft_TIMEOUT)
        set(_ft_TIMEOUT 15)
    endif()
    get_filename_component(_name "${script_path}" NAME_WE)
    set(_test_name "fixture_${_name}")
    add_test(
        NAME "${_test_name}"
        COMMAND "${CMAKE_COMMAND}"
            "-DEXE=$<TARGET_FILE:mslang-c>"
            "-DSCRIPT=${script_path}"
            -P "${PROJECT_SOURCE_DIR}/cmake/run_fixture.cmake"
    )
    set_tests_properties("${_test_name}" PROPERTIES TIMEOUT "${_ft_TIMEOUT}")
endfunction()
