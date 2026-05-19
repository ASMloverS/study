add_library(sample_ext SHARED sample_ext.c)
target_include_directories(sample_ext PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_options(sample_ext PRIVATE ${MSLANG_COMPILE_OPTIONS})
target_link_libraries(sample_ext PRIVATE ms_frontend)

# Ensure MSVC does not mangle the exported C symbol
if(MSVC)
    set_target_properties(sample_ext PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
endif()
