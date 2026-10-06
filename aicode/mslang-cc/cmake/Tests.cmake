# 测试注册中心（T002）：单测函数 + golden 跑器 + fixtures/conformance 扫描
# 契约见 docs/tasks/T002-test-infra.md §2；新增 .ms 用例无需改本文件（Task 4 起自动扫描）
function(ms_add_test name)
  add_executable(test_${name} unit/test_${name}.c)
  target_link_libraries(test_${name} PRIVATE ms_common ms_plat)
  add_test(NAME ${name} COMMAND test_${name})
  set_tests_properties(${name} PROPERTIES LABELS unit)
endfunction()
