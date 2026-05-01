#include <string.h>

#include "ms/buffer.h"
#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/function.h"
#include "ms/runtime/vm.h"
#include "ms/string.h"

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

static size_t buffer_find(const MsBuffer *buffer, const char *needle) {
  size_t needle_length = strlen(needle);
  size_t i;

  if (buffer == NULL || needle == NULL) {
    return 0;
  }
  if (needle_length == 0) {
    return 0;
  }
  if (buffer->length < needle_length) {
    return buffer->length;
  }

  for (i = 0; i + needle_length <= buffer->length; ++i) {
    if (memcmp(buffer->data + i, needle, needle_length) == 0) {
      return i;
    }
  }

  return buffer->length;
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
  TEST_ASSERT(diagnostic->span.line == 1);
  TEST_ASSERT(strcmp(diagnostic->message,
                     "cannot read local variable in its own initializer") == 0);

  ms_diag_list_destroy(&diagnostics);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_lowers_import_statements_with_dedicated_opcodes(void) {
  static const char kSource[] =
      "import core.io\n"
      "from tools.util import helper as alias\n";
  MsChunk chunk;
  MsBuffer disassembly;
  MsDiagnosticList diagnostics;
  size_t import_module_offset;
  size_t import_symbol_offset;

  ms_chunk_init(&chunk);
  ms_buffer_init(&disassembly);
  ms_diag_list_init(&diagnostics);

  TEST_ASSERT(ms_compile_source("<unit>", kSource, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);
  TEST_ASSERT(ms_chunk_disassemble_to_buffer(&chunk, "lowering_basic", &disassembly));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_IMPORT_MODULE"));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_IMPORT_SYMBOL"));
  TEST_ASSERT(buffer_contains(&disassembly, "'core.io'"));
  TEST_ASSERT(buffer_contains(&disassembly, "'tools.util'"));
  TEST_ASSERT(buffer_contains(&disassembly, "'helper'"));
  TEST_ASSERT(buffer_contains(&disassembly, "'alias'"));

  import_module_offset = buffer_find(&disassembly, "OP_IMPORT_MODULE");
  import_symbol_offset = buffer_find(&disassembly, "OP_IMPORT_SYMBOL");
  TEST_ASSERT(import_module_offset < import_symbol_offset);

  ms_diag_list_destroy(&diagnostics);
  ms_buffer_destroy(&disassembly);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_lowers_class_declarations_and_installs_methods(void) {
  static const char kSource[] =
      "class Brunch {\n"
      "  alpha() {\n"
      "    return Brunch\n"
      "  }\n"
      "  beta() {\n"
      "    return 2\n"
      "  }\n"
      "}\n"
      "print Brunch\n";
  static const char kExpectedOutput[] = "<class>\n";
  MsChunk chunk;
  MsBuffer disassembly;
  MsBuffer output;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;
  MsValue class_value = ms_value_nil();
  MsClass *klass = NULL;
  MsTable *methods = NULL;
  MsValue method_value = ms_value_nil();
  MsString *brunch_name = NULL;
  MsString *alpha_name = NULL;
  MsString *beta_name = NULL;
  size_t alpha_offset;
  size_t beta_offset;
  int found = 0;

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
  TEST_ASSERT(buffer_contains(&disassembly, "OP_CLASS"));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_METHOD"));
  TEST_ASSERT(buffer_contains(&disassembly, "<fn alpha>"));
  TEST_ASSERT(buffer_contains(&disassembly, "<fn beta>"));

  alpha_offset = buffer_find(&disassembly, "<fn alpha>");
  beta_offset = buffer_find(&disassembly, "<fn beta>");
  TEST_ASSERT(alpha_offset < disassembly.length);
  TEST_ASSERT(beta_offset < disassembly.length);
  TEST_ASSERT(alpha_offset < beta_offset);

  ms_vm_set_current_module(&vm, &module);
  ms_vm_set_write_callback(&vm, append_output, &output);
  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_OK);
  TEST_ASSERT(output.length == strlen(kExpectedOutput));
  TEST_ASSERT(memcmp(output.data, kExpectedOutput, output.length) == 0);

  brunch_name = ms_string_from_cstr("Brunch");
  alpha_name = ms_string_from_cstr("alpha");
  beta_name = ms_string_from_cstr("beta");
  TEST_ASSERT(brunch_name != NULL);
  TEST_ASSERT(alpha_name != NULL);
  TEST_ASSERT(beta_name != NULL);
  TEST_ASSERT(ms_table_get(&module.globals, brunch_name, &class_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_get_class(class_value, &klass));
  methods = ms_class_methods(klass);
  TEST_ASSERT(methods != NULL);
  TEST_ASSERT(ms_table_count(methods) == 2);
  TEST_ASSERT(ms_table_get(methods, alpha_name, &method_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_is_closure(method_value));
  TEST_ASSERT(ms_table_get(methods, beta_name, &method_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_is_closure(method_value));

  ms_string_free(brunch_name);
  ms_string_free(alpha_name);
  ms_string_free(beta_name);
  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_diag_list_destroy(&diagnostics);
  ms_buffer_destroy(&output);
  ms_buffer_destroy(&disassembly);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_lowers_inheritance_and_super_lookup(void) {
  static const char kSource[] =
      "class Base {\n"
      "  speak() {\n"
      "    print \"base\"\n"
      "  }\n"
      "}\n"
      "class Derived < Base {\n"
      "  call_super() {\n"
      "    super.speak()\n"
      "  }\n"
      "}\n"
      "print Derived\n";
  static const char kExpectedOutput[] = "<class>\n";
  MsChunk chunk;
  MsBuffer disassembly;
  MsBuffer method_disassembly;
  MsBuffer output;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;
  MsString* derived_name = NULL;
  MsString* method_name = NULL;
  MsValue class_value = ms_value_nil();
  MsValue method_value = ms_value_nil();
  MsClass* klass = NULL;
  MsClosure* method = NULL;
  int found = 0;

  ms_chunk_init(&chunk);
  ms_buffer_init(&disassembly);
  ms_buffer_init(&method_disassembly);
  ms_buffer_init(&output);
  ms_diag_list_init(&diagnostics);
  ms_vm_init(&vm);
  ms_module_init(&module, "<unit>");

  TEST_ASSERT(ms_compile_source("<unit>", kSource, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);
  TEST_ASSERT(ms_chunk_disassemble_to_buffer(&chunk, "lowering_basic", &disassembly));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_INHERIT"));

  ms_vm_set_current_module(&vm, &module);
  ms_vm_set_write_callback(&vm, append_output, &output);
  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_OK);
  TEST_ASSERT(output.length == strlen(kExpectedOutput));
  TEST_ASSERT(memcmp(output.data, kExpectedOutput, output.length) == 0);

  derived_name = ms_string_from_cstr("Derived");
  method_name = ms_string_from_cstr("call_super");
  TEST_ASSERT(derived_name != NULL);
  TEST_ASSERT(method_name != NULL);
  TEST_ASSERT(ms_table_get(&module.globals, derived_name, &class_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_get_class(class_value, &klass));
  TEST_ASSERT(ms_table_get(ms_class_methods(klass), method_name, &method_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_get_closure(method_value, &method));
  TEST_ASSERT(ms_chunk_disassemble_to_buffer(&method->function->chunk,
                                             "call_super",
                                             &method_disassembly));
  TEST_ASSERT(buffer_contains(&method_disassembly, "OP_GET_SUPER"));

  ms_string_free(derived_name);
  ms_string_free(method_name);
  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_diag_list_destroy(&diagnostics);
  ms_buffer_destroy(&output);
  ms_buffer_destroy(&method_disassembly);
  ms_buffer_destroy(&disassembly);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_lowers_container_literals_and_runs_build_opcodes(void) {
  static const char kSource[] =
      "print [1, 2]\n"
      "print (3, 4)\n"
      "print {\"alpha\": 5, \"beta\": 6}\n";
  static const char kExpectedOutput[] = "<list>\n<tuple>\n<map>\n";
  MsChunk chunk;
  MsBuffer disassembly;
  MsBuffer output;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;
  size_t list_offset;
  size_t tuple_offset;
  size_t map_offset;

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
  TEST_ASSERT(buffer_contains(&disassembly, "OP_BUILD_LIST"));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_BUILD_TUPLE"));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_BUILD_MAP"));

  list_offset = buffer_find(&disassembly, "OP_BUILD_LIST");
  tuple_offset = buffer_find(&disassembly, "OP_BUILD_TUPLE");
  map_offset = buffer_find(&disassembly, "OP_BUILD_MAP");
  TEST_ASSERT(list_offset < tuple_offset);
  TEST_ASSERT(tuple_offset < map_offset);

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

static int test_lowers_container_indexing_and_list_assignment(void) {
  static const char kSource[] =
      "var list = [1, 2, 3]\n"
      "print list[1]\n"
      "list[1] = 9\n"
      "print list[1]\n"
      "print (4, 5)[0]\n";
  static const char kExpectedOutput[] = "2\n9\n4\n";
  MsChunk chunk;
  MsBuffer disassembly;
  MsBuffer output;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;
  size_t get_offset;
  size_t set_offset;

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
  TEST_ASSERT(buffer_contains(&disassembly, "OP_INDEX_GET"));
  TEST_ASSERT(buffer_contains(&disassembly, "OP_INDEX_SET"));

  get_offset = buffer_find(&disassembly, "OP_INDEX_GET");
  set_offset = buffer_find(&disassembly, "OP_INDEX_SET");
  TEST_ASSERT(get_offset < set_offset);

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

int main(void) {
  TEST_ASSERT(test_lowers_globals_and_block_locals() == 0);
  TEST_ASSERT(test_short_circuit_and_loop_control_flow() == 0);
  TEST_ASSERT(test_reports_resolve_errors_before_lowering() == 0);
  TEST_ASSERT(test_lowers_import_statements_with_dedicated_opcodes() == 0);
  TEST_ASSERT(test_lowers_class_declarations_and_installs_methods() == 0);
  TEST_ASSERT(test_lowers_inheritance_and_super_lookup() == 0);
  TEST_ASSERT(test_lowers_container_literals_and_runs_build_opcodes() == 0);
  TEST_ASSERT(test_lowers_container_indexing_and_list_assignment() == 0);
  return 0;
}
