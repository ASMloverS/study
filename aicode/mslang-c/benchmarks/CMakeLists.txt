find_package(Python3 REQUIRED)

add_custom_target(bench
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/benchmarks/run_all.py
            --exe $<TARGET_FILE:mslang-c>
            --runs 3
    DEPENDS mslang-c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running mslang-c benchmark suite"
)
