#include <string.h>

#include "ms/buffer.h"
#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/vm.h"

#include "test_assert.h"

static int append_output(void *user_data, const char *text, size_t length) {
  MsBuffer *buffer = (MsBuffer *) user_data;

  return ms_buffer_append(buffer, text, length);
}

static int buffer_contains(const MsBuffer *buffer, const char *needle) {
  size_t needle_length = strlen(needle);
  size_t i;

  if (buffer == NULL || needle == NULL) {
    return 0;
  }
  if (needle_length == 0) {
    return 1;
  }
  if (buffer->length < needle_length) {
    return 0;
  }

  for (i = 0; i + needle_length <= buffer->length; ++i) {
    if (memcmp(buffer->data + i, needle, needle_length) == 0) {
      return 1;
    }
  }

  return 0;
}

static int test_lowers_globals_and_block_locals(void) {
  static const char kSource[] =
      "var global = 1\n"
      "{\n"
      "  var local = global + 2\n"
      "  print local\n"
      "}\n"
      "print global\n";
  static const char kExpectedOutput[] = "3\n1\n";
  MsChunk chunk;
  MsBuffer disassembly;
  MsBuffer output;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;

  ms_chunk_init(&chunk);
  ms_buffer_init(&disassembly);
  ms_buffer_init(&output);
  ms_diag_list_init(&diagnostics);
  ms_vm_init(&vm);
  ms_module_init(&module, "<unit>");

  TEST_ASSERT(ms_compile_source("<unit>", kSource, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);
  TEST_ASSERT(ms_chunk_disassemble_to_buffer(&chunk, "lowering_basic", &disassembly));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_DEFINE_GLOBAL"));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_GET_GLOBAL"));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_GET_LOCAL"));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_POP"));

  ms_vm_set_current_module(&vm, &module);
  ms_vm_set_write_callback(&vm, append_output, &output);
  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_OK);
  TEST_ASSERT(output.length == strlen(kExpectedOutput));
  TEST_ASSERT(memcmp(output.data, kExpectedOutput, output.length) == 0);

  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_diag_list_destroy(&diagnostics);
  ms_buffer_destroy(&output);
  ms_buffer_destroy(&disassembly);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_short_circuit_and_loop_control_flow(void) {
  static const char kSource[] =
      "print 0 and 99\n"
      "print 1 or 99\n"
      "var sum = 0\n"
      "var i = 0\n"
      "while (i < 5) {\n"
      "  i = i + 1\n"
      "  if (i == 2) continue\n"
      "  if (i == 5) break\n"
      "  sum = sum + i\n"
      "}\n"
      "print sum\n"
      "for (var j = 0; j < 4; j = j + 1) {\n"
      "  if (j == 1) continue\n"
      "  print j\n"
      "  if (j == 2) break\n"
      "}\n";
  static const char kExpectedOutput[] = "0\n1\n8\n0\n2\n";
  MsChunk chunk;
  MsBuffer output;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;

  ms_chunk_init(&chunk);
  ms_buffer_init(&output);
  ms_diag_list_init(&diagnostics);
  ms_vm_init(&vm);
  ms_module_init(&module, "<unit>");

  TEST_ASSERT(ms_compile_source("<unit>", kSource, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  ms_vm_set_current_module(&vm, &module);
  ms_vm_set_write_callback(&vm, append_output, &output);
  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_OK);
  TEST_ASSERT(output.length == strlen(kExpectedOutput));
  TEST_ASSERT(memcmp(output.data, kExpectedOutput, output.length) == 0);

  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_diag_list_destroy(&diagnostics);
  ms_buffer_destroy(&output);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_reports_resolve_errors_before_lowering(void) {
  static const char kSource[] =
      "{ var value = value }\n";
  MsChunk chunk;
  MsDiagnosticList diagnostics;
  const MsDiagnostic *diagnostic;

  ms_chunk_init(&chunk);
  ms_diag_list_init(&diagnostics);

  TEST_ASSERT(ms_compile_source("<unit>", kSource, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_RESOLVE_ERROR);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 1);

  diagnostic = ms_diag_list_at(&diagnostics, 0);
  TEST_ASSERT(diagnostic != NULL);
  TEST_ASSERT(strcmp(diagnostic->phase, "resolve") == 0);
  TEST_ASSERT(strcmp(diagnostic->code, "MS3004") == 0);
  TEST_ASSERT(strcmp(diagnostic->message,
                     "cannot read local variable in its own initializer") == 0);

  ms_diag_list_destroy(&diagnostics);
  ms_chunk_destroy(&chunk);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_lowers_globals_and_block_locals() == 0);
  TEST_ASSERT(test_short_circuit_and_loop_control_flow() == 0);
  TEST_ASSERT(test_reports_resolve_errors_before_lowering() == 0);
  return 0;
}
