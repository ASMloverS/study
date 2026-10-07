# 测试注册中心（T002）：单测函数 + golden 跑器 + fixtures/conformance 扫描
# 契约见 docs/tasks/T002-test-infra.md §2：新增 .ms+.out 用例只需落盘，reconfigure 自动注册
function(ms_add_test name)
  add_executable(test_${name} unit/test_${name}.c)
  target_link_libraries(test_${name} PRIVATE ms_common ms_plat ${ARGN})
  add_test(NAME ${name} COMMAND test_${name})
  set_tests_properties(${name} PROPERTIES LABELS unit)
endfunction()

# golden 跑器与用例扫描收进函数：include() 直通变量作用域，裸 foreach 会把循环变量泄入包含者
function(ms_register_golden_cases)
  # golden 跑器：每个 CTest 项跑一个用例；.timeout 由跑器运行时读取，此处仅设兜底上限
  add_executable(test_runner tools/test_runner.c)

  file(GLOB_RECURSE MS_FIXTURE_CASES CONFIGURE_DEPENDS LIST_DIRECTORIES false
       ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/*.ms)
  foreach(case_file IN LISTS MS_FIXTURE_CASES)
    file(RELATIVE_PATH case_rel ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures ${case_file})
    string(REGEX REPLACE "\\.ms$" "" case_stem "${case_rel}")
    string(REPLACE "/" "_" case_name "${case_stem}")
    add_test(NAME fixture_${case_name} COMMAND test_runner $<TARGET_FILE:ms> "${case_file}")
    set_tests_properties(fixture_${case_name} PROPERTIES LABELS fixture TIMEOUT 60)
  endforeach()

  file(GLOB_RECURSE MS_CONFORMANCE_CASES CONFIGURE_DEPENDS LIST_DIRECTORIES false
       ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/*.ms)
  foreach(case_file IN LISTS MS_CONFORMANCE_CASES)
    file(RELATIVE_PATH case_rel ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance ${case_file})
    string(REGEX REPLACE "\\.ms$" "" case_stem "${case_rel}")
    string(REPLACE "/" "_" case_name "${case_stem}")
    add_test(NAME conformance_${case_name} COMMAND test_runner $<TARGET_FILE:ms> "${case_file}")
    set_tests_properties(conformance_${case_name} PROPERTIES LABELS conformance TIMEOUT 60)
  endforeach()
endfunction()

ms_register_golden_cases()
