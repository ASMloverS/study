#include <string.h>

#include "ms/buffer.h"
#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/runtime/function.h"
#include "ms/runtime/vm.h"
#include "ms/string.h"
#include "ms/table.h"
#include "ms/value.h"

#include "test_assert.h"

static int append_output(void* user_data, const char* text, size_t length) {
  MsBuffer* buffer = (MsBuffer*) user_data;

  return ms_buffer_append(buffer, text, length);
}

static int compile_and_run(const char* source, const char* expected_output) {
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

static int test_class_allocation_initializes_name_and_methods(void) {
  MsClass* klass;
  MsString* method_name;
  MsTable* methods;
  int inserted_new = 0;

  klass = ms_class_new("Widget", strlen("Widget"), NULL);
  TEST_ASSERT(klass != NULL);
  TEST_ASSERT(klass->object.type == MS_OBJ_CLASS);
  TEST_ASSERT(ms_class_name(klass) != NULL);
  TEST_ASSERT(strcmp(ms_class_name(klass)->bytes, "Widget") == 0);
  TEST_ASSERT(ms_class_superclass(klass) == NULL);

  methods = ms_class_methods(klass);
  TEST_ASSERT(methods != NULL);
  TEST_ASSERT(ms_table_count(methods) == 0);
  TEST_ASSERT(ms_table_capacity(methods) == 0);

  method_name = ms_string_from_cstr("run");
  TEST_ASSERT(method_name != NULL);
  TEST_ASSERT(ms_table_set(methods, method_name, ms_value_nil(), &inserted_new));
  TEST_ASSERT(inserted_new);
  TEST_ASSERT(ms_table_count(methods) == 1);

  ms_class_free(klass);
  ms_string_free(method_name);
  return 0;
}

static int test_instance_initializes_empty_field_table(void) {
  MsClass* klass;
  MsInstance* instance;
  MsTable* fields;

  klass = ms_class_new("Widget", strlen("Widget"), NULL);
  TEST_ASSERT(klass != NULL);

  instance = ms_instance_new(klass);
  TEST_ASSERT(instance != NULL);
  TEST_ASSERT(instance->object.type == MS_OBJ_INSTANCE);
  TEST_ASSERT(ms_instance_class(instance) == klass);

  fields = ms_instance_fields(instance);
  TEST_ASSERT(fields != NULL);
  TEST_ASSERT(ms_table_count(fields) == 0);
  TEST_ASSERT(ms_table_capacity(fields) == 0);

  ms_instance_free(instance);
  ms_class_free(klass);
  return 0;
}

static int test_instance_fields_do_not_mutate_class_methods(void) {
  MsClass* klass;
  MsInstance* instance;
  MsTable* methods;
  MsTable* fields;
  MsFunction* function;
  MsClosure* method;
  MsString* name;
  MsString* field_value;
  MsValue stored_value = ms_value_nil();
  int inserted_new = 0;
  int found = 0;

  klass = ms_class_new("Widget", strlen("Widget"), NULL);
  TEST_ASSERT(klass != NULL);
  instance = ms_instance_new(klass);
  TEST_ASSERT(instance != NULL);
  methods = ms_class_methods(klass);
  fields = ms_instance_fields(instance);
  TEST_ASSERT(methods != NULL);
  TEST_ASSERT(fields != NULL);

  function = ms_function_new("label", strlen("label"), 0);
  TEST_ASSERT(function != NULL);
  method = ms_closure_new(function);
  TEST_ASSERT(method != NULL);
  name = ms_string_from_cstr("label");
  field_value = ms_string_from_cstr("field");
  TEST_ASSERT(name != NULL);
  TEST_ASSERT(field_value != NULL);

  TEST_ASSERT(ms_table_set(methods,
                           name,
                           ms_value_object((MsObject*) method),
                           &inserted_new));
  TEST_ASSERT(inserted_new);
  TEST_ASSERT(ms_table_set(fields,
                           name,
                           ms_value_object((MsObject*) field_value),
                           &inserted_new));
  TEST_ASSERT(inserted_new);

  TEST_ASSERT(ms_table_get(fields, name, &stored_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_is_string(stored_value));
  TEST_ASSERT(ms_table_get(methods, name, &stored_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_is_closure(stored_value));

  ms_string_free(field_value);
  ms_string_free(name);
  ms_closure_free(method);
  ms_function_free(function);
  ms_instance_free(instance);
  ms_class_free(klass);
  return 0;
}

static int test_bound_method_stores_receiver(void) {
  MsClass* klass;
  MsInstance* instance;
  MsFunction* function;
  MsClosure* method;
  MsBoundMethod* bound_method;
  MsValue receiver_value;
  MsInstance* out_instance = NULL;

  klass = ms_class_new("Widget", strlen("Widget"), NULL);
  TEST_ASSERT(klass != NULL);

  instance = ms_instance_new(klass);
  TEST_ASSERT(instance != NULL);

  function = ms_function_new("run", strlen("run"), 0);
  TEST_ASSERT(function != NULL);

  method = ms_closure_new(function);
  TEST_ASSERT(method != NULL);

  receiver_value = ms_value_object((MsObject*) instance);
  bound_method = ms_bound_method_new(receiver_value, method);
  TEST_ASSERT(bound_method != NULL);
  TEST_ASSERT(bound_method->object.type == MS_OBJ_BOUND_METHOD);
  TEST_ASSERT(ms_value_is_bound_method(ms_value_object((MsObject*) bound_method)));
  TEST_ASSERT(ms_value_is_instance(ms_bound_method_receiver(bound_method)));
  TEST_ASSERT(ms_value_get_instance(ms_bound_method_receiver(bound_method),
                                    &out_instance));
  TEST_ASSERT(out_instance == instance);
  TEST_ASSERT(ms_bound_method_method(bound_method) == method);

  ms_bound_method_free(bound_method);
  ms_closure_free(method);
  ms_function_free(function);
  ms_instance_free(instance);
  ms_class_free(klass);
  return 0;
}

static int test_bound_method_calls_write_through_self(void) {
  static const char kSource[] =
      "class Box {\n"
      "  set(value) {\n"
      "    self.value = value\n"
      "  }\n"
      "  show() {\n"
      "    print self.value\n"
      "  }\n"
      "}\n"
      "var box = Box()\n"
      "box.value = \"tea\"\n"
      "var show = box.show\n"
      "show()\n"
      "var set = box.set\n"
      "set(\"coffee\")\n"
      "show()\n";

  TEST_ASSERT(compile_and_run(kSource, "tea\ncoffee\n") == 0);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_class_allocation_initializes_name_and_methods() == 0);
  TEST_ASSERT(test_instance_initializes_empty_field_table() == 0);
  TEST_ASSERT(test_instance_fields_do_not_mutate_class_methods() == 0);
  TEST_ASSERT(test_bound_method_stores_receiver() == 0);
  TEST_ASSERT(test_bound_method_calls_write_through_self() == 0);
  return 0;
}