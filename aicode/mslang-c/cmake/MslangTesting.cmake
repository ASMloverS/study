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
