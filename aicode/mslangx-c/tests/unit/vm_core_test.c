#include <string.h>
#include <stdlib.h>

#include "ms/buffer.h"
#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/opcode.h"
#include "ms/runtime/vm.h"
#include "ms/string.h"
#include "ms/value.h"

#include "test_assert.h"

static int append_output(void *user_data, const char *text, size_t length) {
  MsBuffer *buffer = (MsBuffer *) user_data;

  return ms_buffer_append(buffer, text, length);
}

static int expect_output_equals(const MsBuffer *buffer, const char *expected) {
  size_t expected_length = strlen(expected);

  TEST_ASSERT(buffer->length == expected_length);
  TEST_ASSERT(memcmp(buffer->data, expected, expected_length) == 0);
  return 0;
}

static MsCallResult test_native_noop(MsVM* vm, int argc, const MsValue* argv) {
  (void) vm;
  (void) argc;
  (void) argv;

  return ms_call_result_ok(ms_value_nil());
}

static int test_arithmetic_and_comparison(void) {
  MsVM vm;
  MsModule module;
  MsChunk chunk;
  MsBuffer output;
  uint8_t one = 0;
  uint8_t two = 0;
  uint8_t three = 0;

  ms_vm_init(&vm);
  ms_module_init(&module, "unit");
  ms_chunk_init(&chunk);
  ms_buffer_init(&output);

  ms_vm_set_current_module(&vm, &module);
  ms_vm_set_write_callback(&vm, append_output, &output);

  TEST_ASSERT(ms_chunk_add_constant(&chunk, ms_value_number(1.0), &one));
  TEST_ASSERT(ms_chunk_add_constant(&chunk, ms_value_number(2.0), &two));
  TEST_ASSERT(ms_chunk_add_constant(&chunk, ms_value_number(3.0), &three));

  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 10));
  TEST_ASSERT(ms_chunk_write(&chunk, one, 10));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 10));
  TEST_ASSERT(ms_chunk_write(&chunk, two, 10));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_ADD, 10));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_PRINT, 10));

  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 11));
  TEST_ASSERT(ms_chunk_write(&chunk, three, 11));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 11));
  TEST_ASSERT(ms_chunk_write(&chunk, two, 11));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_GREATER, 11));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_PRINT, 11));

  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_NIL, 12));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_NOT, 12));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_PRINT, 12));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_RETURN, 13));

  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&vm.diagnostics) == 0);
  TEST_ASSERT(expect_output_equals(&output, "3\ntrue\ntrue\n") == 0);

  ms_buffer_destroy(&output);
  ms_chunk_destroy(&chunk);
  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  return 0;
}

static int test_globals_and_jumps(void) {
  MsVM vm;
  MsModule module;
  MsChunk chunk;
  MsBuffer output;
  MsString *global_name;
  MsString *then_value;
  MsString *else_value;
  uint8_t forty_one = 0;
  uint8_t one = 0;
  uint8_t name_index = 0;
  uint8_t then_index = 0;
  uint8_t else_index = 0;
  size_t skip_then = 0;
  size_t skip_else = 0;
  MsValue stored = ms_value_nil();
  int found = 0;

  ms_vm_init(&vm);
  ms_module_init(&module, "unit");
  ms_chunk_init(&chunk);
  ms_buffer_init(&output);

  global_name = ms_string_from_cstr("answer");
  then_value = ms_string_from_cstr("then");
  else_value = ms_string_from_cstr("else");

  TEST_ASSERT(global_name != NULL);
  TEST_ASSERT(then_value != NULL);
  TEST_ASSERT(else_value != NULL);

  ms_vm_set_current_module(&vm, &module);
  ms_vm_set_write_callback(&vm, append_output, &output);

  TEST_ASSERT(ms_chunk_add_constant(&chunk, ms_value_number(41.0), &forty_one));
  TEST_ASSERT(ms_chunk_add_constant(&chunk, ms_value_number(1.0), &one));
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_object((MsObject *) global_name),
                                    &name_index));
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_object((MsObject *) then_value),
                                    &then_index));
  TEST_ASSERT(ms_chunk_add_constant(&chunk,
                                    ms_value_object((MsObject *) else_value),
                                    &else_index));

  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 20));
  TEST_ASSERT(ms_chunk_write(&chunk, forty_one, 20));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_DEFINE_GLOBAL, 20));
  TEST_ASSERT(ms_chunk_write(&chunk, name_index, 20));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_GET_GLOBAL, 21));
  TEST_ASSERT(ms_chunk_write(&chunk, name_index, 21));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 21));
  TEST_ASSERT(ms_chunk_write(&chunk, one, 21));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_ADD, 21));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_SET_GLOBAL, 21));
  TEST_ASSERT(ms_chunk_write(&chunk, name_index, 21));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_POP, 21));

  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_FALSE, 22));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_JUMP_IF_FALSE, 22));
  skip_then = ms_chunk_code_count(&chunk);
  TEST_ASSERT(ms_chunk_write_short(&chunk, 0xffffu, 22));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_POP, 22));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 23));
  TEST_ASSERT(ms_chunk_write(&chunk, then_index, 23));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_PRINT, 23));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_JUMP, 23));
  skip_else = ms_chunk_code_count(&chunk);
  TEST_ASSERT(ms_chunk_write_short(&chunk, 0xffffu, 23));
  TEST_ASSERT(ms_chunk_patch_short(&chunk,
                                   skip_then,
                                   (uint16_t) (ms_chunk_code_count(&chunk) -
                                               skip_then - 2)));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_POP, 24));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_CONSTANT, 24));
  TEST_ASSERT(ms_chunk_write(&chunk, else_index, 24));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_PRINT, 24));
  TEST_ASSERT(ms_chunk_patch_short(&chunk,
                                   skip_else,
                                   (uint16_t) (ms_chunk_code_count(&chunk) -
                                               skip_else - 2)));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_GET_GLOBAL, 25));
  TEST_ASSERT(ms_chunk_write(&chunk, name_index, 25));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_PRINT, 25));
  TEST_ASSERT(ms_chunk_write(&chunk, MS_OP_RETURN, 26));

  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&vm.diagnostics) == 0);
  TEST_ASSERT(expect_output_equals(&output, "else\n42\n") == 0);
  TEST_ASSERT(ms_table_get(&module.globals, global_name, &stored, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_equals(stored, ms_value_number(42.0)));

  ms_string_free(else_value);
  ms_string_free(then_value);
  ms_string_free(global_name);
  ms_buffer_destroy(&output);
  ms_chunk_destroy(&chunk);
  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  return 0;
}

static int test_gc_marks_stack_frames_and_closures(void) {
  MsVM vm;
  MsModule module;
  MsFunction* function;
  MsClosure* closure;
  MsString* stack_string;
  MsString* receiver_string;
  MsString* constant_string;
  MsValue* stack_slots;
  MsCallFrame* frame;
  uint8_t constant_index = 0;

  ms_vm_init(&vm);
  ms_module_init(&module, "unit");

  stack_string = ms_string_from_cstr("stack");
  receiver_string = ms_string_from_cstr("receiver");
  constant_string = ms_string_from_cstr("constant");
  function = ms_function_new("root", strlen("root"), 0);

  TEST_ASSERT(stack_string != NULL);
  TEST_ASSERT(receiver_string != NULL);
  TEST_ASSERT(constant_string != NULL);
  TEST_ASSERT(function != NULL);
  stack_slots = (MsValue*) calloc(1, sizeof(*stack_slots));
  frame = (MsCallFrame*) calloc(1, sizeof(*frame));
  TEST_ASSERT(stack_slots != NULL);
  TEST_ASSERT(frame != NULL);

  TEST_ASSERT(ms_chunk_add_constant(&function->chunk,
                                    ms_value_object((MsObject*) constant_string),
                                    &constant_index));
  closure = ms_closure_new(function);
  TEST_ASSERT(closure != NULL);

  stack_slots[0] = ms_value_object((MsObject*) stack_string);
  frame->chunk = &function->chunk;
  frame->closure = closure;
  frame->module = &module;
  frame->ip = 0;
  frame->stack_base = 0;
  frame->receiver = ms_value_object((MsObject*) receiver_string);
  frame->has_receiver = 1;

  vm.stack = stack_slots;
  vm.stack_count = 1;
  vm.stack_capacity = 1;
  vm.frames = frame;
  vm.frame_count = 1;
  vm.frame_capacity = 1;
  vm.open_upvalues = NULL;
  vm.current_module = &module;

  ms_vm_gc_mark_roots(&vm);

  TEST_ASSERT(stack_string->object.marked == 1);
  TEST_ASSERT(receiver_string->object.marked == 1);
  TEST_ASSERT(constant_string->object.marked == 1);
  TEST_ASSERT(function->object.marked == 1);
  TEST_ASSERT(function->name->object.marked == 1);
  TEST_ASSERT(closure->object.marked == 1);
  TEST_ASSERT(module.object.marked == 1);

  ms_closure_free(closure);
  ms_function_free(function);
  ms_string_free(constant_string);
  ms_string_free(receiver_string);
  ms_string_free(stack_string);
  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  return 0;
}

static int test_runtime_error_closes_open_upvalues(void) {
  static const char kSource[] =
      "var saved = nil\n"
      "fn outer() {\n"
      "  var value = \"captured\"\n"
      "  saved = fn() {\n"
      "    return value\n"
      "  }\n"
      "  1()\n"
      "}\n"
      "outer()\n";
  MsChunk chunk;
  MsDiagnosticList diagnostics;
  MsVM vm;
  MsModule module;
  MsString* saved_key;
  MsValue saved_value = ms_value_nil();
  MsClosure* saved_closure = NULL;
  MsUpvalue* saved_upvalue = NULL;
  MsString* captured_string = NULL;
  int found = 0;

  ms_chunk_init(&chunk);
  ms_diag_list_init(&diagnostics);
  ms_vm_init(&vm);
  ms_module_init(&module, "unit");

  TEST_ASSERT(ms_compile_source("<unit>", kSource, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  ms_vm_set_current_module(&vm, &module);
  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_RUNTIME_ERROR);
  TEST_ASSERT(vm.open_upvalues == NULL);

  saved_key = ms_string_from_cstr("saved");
  TEST_ASSERT(saved_key != NULL);
  TEST_ASSERT(ms_table_get(&module.globals, saved_key, &saved_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_get_closure(saved_value, &saved_closure));
  TEST_ASSERT(saved_closure != NULL);
  TEST_ASSERT(saved_closure->upvalues != NULL);
  TEST_ASSERT(saved_closure->upvalues[0] != NULL);
  saved_upvalue = saved_closure->upvalues[0];
  TEST_ASSERT(saved_upvalue->location == &saved_upvalue->closed);
  TEST_ASSERT(ms_value_get_string(saved_upvalue->closed, &captured_string));
  TEST_ASSERT(captured_string != NULL);
  TEST_ASSERT(strcmp(captured_string->bytes, "captured") == 0);

  ms_string_free(saved_key);
  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_diag_list_destroy(&diagnostics);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_module_cache_marks_native_registry_entries(void) {
  MsVM vm;
  MsModule* module;
  MsString* lookup_key;
  MsValue stored_value = ms_value_nil();
  MsNativeFunction* native_function = NULL;
  MsString* tracked_key = NULL;
  int found = 0;
  int inserted_new = 0;
  size_t i;

  ms_vm_init(&vm);
  module = ms_vm_get_or_create_module(&vm, "vm_core_test_tmp/native.ms", &inserted_new);
  TEST_ASSERT(module != NULL);
  TEST_ASSERT(inserted_new);
  TEST_ASSERT(ms_vm_define_native(&vm, module, "noop", 0, test_native_noop) != 0);

  lookup_key = ms_string_from_cstr("noop");
  TEST_ASSERT(lookup_key != NULL);
  TEST_ASSERT(ms_table_get(&module->globals, lookup_key, &stored_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_get_native_function(stored_value, &native_function));
  TEST_ASSERT(native_function != NULL);
  for (i = 0; i < module->globals.capacity; ++i) {
    if (module->globals.entries[i].key != NULL) {
      tracked_key = module->globals.entries[i].key;
      break;
    }
  }
  TEST_ASSERT(tracked_key != NULL);

  ms_vm_gc_collect(&vm);

  TEST_ASSERT(vm.module_cache.count == 1);
  TEST_ASSERT(ms_table_get(&module->globals, tracked_key, &stored_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_get_native_function(stored_value, &native_function));
  TEST_ASSERT(vm.gc.collection_count == 1);
  TEST_ASSERT(vm.gc.free_count == 0);

  ms_vm_destroy(&vm);
  ms_native_function_free(native_function);
  ms_string_free(tracked_key);
  ms_string_free(lookup_key);
  return 0;
}

static int expect_runtime_error(const MsChunk *chunk,
                                const char *code,
                                const char *message,
                                int line) {
  MsVM vm;
  MsModule module;
  const MsDiagnostic *diagnostic;

  ms_vm_init(&vm);
  ms_module_init(&module, "unit");
  ms_vm_set_current_module(&vm, &module);

  TEST_ASSERT(ms_vm_run_chunk(&vm, chunk) == MS_VM_RESULT_RUNTIME_ERROR);
  TEST_ASSERT(ms_diag_list_count(&vm.diagnostics) == 1);

  diagnostic = ms_diag_list_at(&vm.diagnostics, 0);
  TEST_ASSERT(diagnostic != NULL);
  TEST_ASSERT(strcmp(diagnostic->phase, "runtime") == 0);
  TEST_ASSERT(strcmp(diagnostic->code, code) == 0);
  TEST_ASSERT(strcmp(diagnostic->message, message) == 0);
  TEST_ASSERT(diagnostic->span.line == line);

  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  return 0;
}

static int test_runtime_errors(void) {
  MsChunk stack_underflow;
  MsChunk invalid_jump;
  MsChunk type_mismatch;
  MsChunk missing_global;
  MsChunk invalid_opcode;
  MsString *name;
  MsString *text;
  uint8_t name_index = 0;
  uint8_t text_index = 0;
  uint8_t number_index = 0;

  ms_chunk_init(&stack_underflow);
  ms_chunk_init(&invalid_jump);
  ms_chunk_init(&type_mismatch);
  ms_chunk_init(&missing_global);
  ms_chunk_init(&invalid_opcode);

  TEST_ASSERT(ms_chunk_write(&stack_underflow, MS_OP_ADD, 30));
  TEST_ASSERT(ms_chunk_write(&stack_underflow, MS_OP_RETURN, 30));
  TEST_ASSERT(expect_runtime_error(&stack_underflow,
                                   "MS4002",
                                   "stack underflow",
                                   30) == 0);

  TEST_ASSERT(ms_chunk_write(&invalid_jump, MS_OP_FALSE, 31));
  TEST_ASSERT(ms_chunk_write(&invalid_jump, MS_OP_JUMP_IF_FALSE, 31));
  TEST_ASSERT(ms_chunk_write_short(&invalid_jump, 300u, 31));
  TEST_ASSERT(ms_chunk_write(&invalid_jump, MS_OP_RETURN, 31));
  TEST_ASSERT(expect_runtime_error(&invalid_jump,
                                   "MS4005",
                                   "invalid jump target",
                                   31) == 0);

  text = ms_string_from_cstr("oops");
  TEST_ASSERT(text != NULL);
  TEST_ASSERT(ms_chunk_add_constant(&type_mismatch,
                                    ms_value_number(1.0),
                                    &number_index));
  TEST_ASSERT(ms_chunk_add_constant(&type_mismatch,
                                    ms_value_object((MsObject *) text),
                                    &text_index));
  TEST_ASSERT(ms_chunk_write(&type_mismatch, MS_OP_CONSTANT, 32));
  TEST_ASSERT(ms_chunk_write(&type_mismatch, number_index, 32));
  TEST_ASSERT(ms_chunk_write(&type_mismatch, MS_OP_CONSTANT, 32));
  TEST_ASSERT(ms_chunk_write(&type_mismatch, text_index, 32));
  TEST_ASSERT(ms_chunk_write(&type_mismatch, MS_OP_ADD, 32));
  TEST_ASSERT(ms_chunk_write(&type_mismatch, MS_OP_RETURN, 32));
  TEST_ASSERT(expect_runtime_error(&type_mismatch,
                                   "MS4003",
                                   "operands must be numbers",
                                   32) == 0);

  name = ms_string_from_cstr("missing");
  TEST_ASSERT(name != NULL);
  TEST_ASSERT(ms_chunk_add_constant(&missing_global,
                                    ms_value_object((MsObject *) name),
                                    &name_index));
  TEST_ASSERT(ms_chunk_write(&missing_global, MS_OP_GET_GLOBAL, 33));
  TEST_ASSERT(ms_chunk_write(&missing_global, name_index, 33));
  TEST_ASSERT(ms_chunk_write(&missing_global, MS_OP_RETURN, 33));
  TEST_ASSERT(expect_runtime_error(&missing_global,
                                   "MS4004",
                                   "undefined global",
                                   33) == 0);

  TEST_ASSERT(ms_chunk_write(&invalid_opcode, 0xffu, 34));
  TEST_ASSERT(ms_chunk_write(&invalid_opcode, MS_OP_RETURN, 34));
  TEST_ASSERT(expect_runtime_error(&invalid_opcode,
                                   "MS4001",
                                   "invalid opcode stream",
                                   34) == 0);

  ms_string_free(name);
  ms_string_free(text);
  ms_chunk_destroy(&invalid_opcode);
  ms_chunk_destroy(&missing_global);
  ms_chunk_destroy(&type_mismatch);
  ms_chunk_destroy(&invalid_jump);
  ms_chunk_destroy(&stack_underflow);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_arithmetic_and_comparison() == 0);
  TEST_ASSERT(test_globals_and_jumps() == 0);
  TEST_ASSERT(test_gc_marks_stack_frames_and_closures() == 0);
  TEST_ASSERT(test_runtime_error_closes_open_upvalues() == 0);
  TEST_ASSERT(test_module_cache_marks_native_registry_entries() == 0);
  TEST_ASSERT(test_runtime_errors() == 0);
  return 0;
}
