#include <string.h>

#include "ms/buffer.h"
#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/vm.h"
#include "ms/value.h"

#include "test_assert.h"

typedef int (*MsNativeSetupFn)(MsVM* vm, MsModule* module);

static int append_output(void* user_data, const char* text, size_t length) {
  MsBuffer* buffer = (MsBuffer*) user_data;

  return ms_buffer_append(buffer, text, length);
}

static int compile_and_run_with_setup(const char* source,
                                      const char* expected_output,
                                      MsNativeSetupFn setup_fn) {
  MsChunk chunk;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;
  MsBuffer output;

  ms_chunk_init(&chunk);
  ms_diag_list_init(&diagnostics);
  ms_vm_init(&vm);
  ms_module_init(&module, "<unit>");
  ms_buffer_init(&output);

  TEST_ASSERT(ms_compile_source("<unit>", source, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  ms_vm_set_current_module(&vm, &module);
  ms_vm_set_write_callback(&vm, append_output, &output);
  if (setup_fn != NULL) {
    TEST_ASSERT(setup_fn(&vm, &module));
  }
  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_OK);
  TEST_ASSERT(output.length == strlen(expected_output));
  TEST_ASSERT(memcmp(output.data, expected_output, output.length) == 0);

  ms_buffer_destroy(&output);
  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_diag_list_destroy(&diagnostics);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int compile_and_run(const char* source,
                           const char* expected_output) {
  return compile_and_run_with_setup(source, expected_output, NULL);
}

static int expect_runtime_error(const char* source,
                                const char* code,
                                const char* message,
                                MsNativeSetupFn setup_fn) {
  MsChunk chunk;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;
  const MsDiagnostic* diagnostic;

  ms_chunk_init(&chunk);
  ms_diag_list_init(&diagnostics);
  ms_vm_init(&vm);
  ms_module_init(&module, "<unit>");

  TEST_ASSERT(ms_compile_source("<unit>", source, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  ms_vm_set_current_module(&vm, &module);
  if (setup_fn != NULL) {
    TEST_ASSERT(setup_fn(&vm, &module));
  }
  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_RUNTIME_ERROR);
  TEST_ASSERT(ms_diag_list_count(&vm.diagnostics) == 1);
  diagnostic = ms_diag_list_at(&vm.diagnostics, 0);
  TEST_ASSERT(diagnostic != NULL);
  TEST_ASSERT(strcmp(diagnostic->phase, "runtime") == 0);
  TEST_ASSERT(strcmp(diagnostic->code, code) == 0);
  TEST_ASSERT(strcmp(diagnostic->message, message) == 0);

  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_diag_list_destroy(&diagnostics);
  ms_chunk_destroy(&chunk);
  return 0;
}

static MsCallResult native_add(MsVM* vm, int argc, const MsValue* argv) {
  double left = 0.0;
  double right = 0.0;

  (void) vm;
  if (argc != 2 || !ms_value_get_number(argv[0], &left) ||
      !ms_value_get_number(argv[1], &right)) {
    return ms_call_result_error();
  }
  return ms_call_result_ok(ms_value_number(left + right));
}

static int register_native_add(MsVM* vm, MsModule* module) {
  return ms_vm_define_native(vm, module, "native_add", 2, native_add);
}

static int test_recursive_function_returns_value(void) {
  static const char kSource[] =
      "fn fact(n) {\n"
      "  if (n <= 1) return 1\n"
      "  return n * fact(n - 1)\n"
      "}\n"
      "print fact(5)\n";

  TEST_ASSERT(compile_and_run(kSource, "120\n") == 0);
  return 0;
}

static int test_function_expression_and_closure_capture(void) {
  static const char kSource[] =
      "fn make_counter(start) {\n"
      "  var value = start\n"
      "  return fn() {\n"
      "    value = value + 1\n"
      "    return value\n"
      "  }\n"
      "}\n"
      "var counter = make_counter(10)\n"
      "print counter()\n"
      "print counter()\n";

  TEST_ASSERT(compile_and_run(kSource, "11\n12\n") == 0);
  return 0;
}

static int test_nested_closure_shadowing(void) {
  static const char kSource[] =
      "fn outer() {\n"
      "  var value = \"outer\"\n"
      "  fn middle() {\n"
      "    var value = \"middle\"\n"
      "    return fn() {\n"
      "      return value\n"
      "    }\n"
      "  }\n"
      "  return middle()\n"
      "}\n"
      "var closure = outer()\n"
      "print closure()\n";

  TEST_ASSERT(compile_and_run(kSource, "middle\n") == 0);
  return 0;
}

static int test_native_function_abi(void) {
  static const char kSource[] =
      "print native_add(20, 22)\n";

  TEST_ASSERT(compile_and_run_with_setup(kSource,
                                         "42\n",
                                         register_native_add) == 0);
  return 0;
}

static int test_reports_non_callable_runtime_error(void) {
  static const char kSource[] =
      "var value = 1\n"
      "value()\n";

  TEST_ASSERT(expect_runtime_error(kSource,
                                   "MS4006",
                                   "value is not callable",
                                   NULL) == 0);
  return 0;
}

static int test_reports_wrong_arity_runtime_error(void) {
  static const char kSource[] =
      "fn identity(value) {\n"
      "  return value\n"
      "}\n"
      "identity()\n";

  TEST_ASSERT(expect_runtime_error(kSource,
                                   "MS4007",
                                   "wrong number of arguments",
                                   NULL) == 0);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_recursive_function_returns_value() == 0);
  TEST_ASSERT(test_function_expression_and_closure_capture() == 0);
  TEST_ASSERT(test_nested_closure_shadowing() == 0);
  TEST_ASSERT(test_native_function_abi() == 0);
  TEST_ASSERT(test_reports_non_callable_runtime_error() == 0);
  TEST_ASSERT(test_reports_wrong_arity_runtime_error() == 0);
  return 0;
}