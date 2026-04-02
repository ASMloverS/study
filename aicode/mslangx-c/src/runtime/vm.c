#include "ms/runtime/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ms/frontend/resolution_table.h"
#include "ms/object.h"
#include "ms/runtime/opcode.h"
#include "ms/string.h"

static MsCallFrame* ms_vm_current_frame(MsVM* vm) {
  if (vm == NULL || vm->frame_count == 0) {
    return NULL;
  }

  return &vm->frames[vm->frame_count - 1];
}

static int ms_vm_ensure_stack(MsVM* vm, size_t min_capacity) {
  MsValue* stack;
  MsValue* old_stack;
  size_t new_capacity;

  if (vm == NULL) {
    return 0;
  }
  if (min_capacity <= vm->stack_capacity) {
    return 1;
  }

  old_stack = vm->stack;
  new_capacity = vm->stack_capacity == 0 ? 16 : vm->stack_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  stack = (MsValue*) realloc(vm->stack, new_capacity * sizeof(*stack));
  if (stack == NULL) {
    return 0;
  }

  vm->stack = stack;
  vm->stack_capacity = new_capacity;
  if (old_stack != NULL && old_stack != stack) {
    MsUpvalue* upvalue = vm->open_upvalues;

    while (upvalue != NULL) {
      if (upvalue->location >= old_stack &&
          upvalue->location < old_stack + vm->stack_count) {
        ptrdiff_t index = upvalue->location - old_stack;

        upvalue->location = stack + index;
      }
      upvalue = upvalue->next;
    }
  }
  return 1;
}

static int ms_vm_ensure_frames(MsVM* vm, size_t min_capacity) {
  MsCallFrame* frames;
  size_t new_capacity;

  if (vm == NULL) {
    return 0;
  }
  if (min_capacity <= vm->frame_capacity) {
    return 1;
  }

  new_capacity = vm->frame_capacity == 0 ? 8 : vm->frame_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  frames = (MsCallFrame*) realloc(vm->frames, new_capacity * sizeof(*frames));
  if (frames == NULL) {
    return 0;
  }

  vm->frames = frames;
  vm->frame_capacity = new_capacity;
  return 1;
}

static void ms_vm_reset(MsVM* vm) {
  if (vm == NULL) {
    return;
  }

  vm->stack_count = 0;
  vm->frame_count = 0;
  vm->open_upvalues = NULL;
  ms_diag_list_clear(&vm->diagnostics);
}

static int ms_vm_push(MsVM* vm, MsValue value) {
  if (vm == NULL || !ms_vm_ensure_stack(vm, vm->stack_count + 1)) {
    return 0;
  }

  vm->stack[vm->stack_count] = value;
  vm->stack_count += 1;
  return 1;
}

static int ms_vm_pop(MsVM* vm, MsValue* out_value) {
  if (vm == NULL || vm->stack_count == 0) {
    return 0;
  }

  vm->stack_count -= 1;
  if (out_value != NULL) {
    *out_value = vm->stack[vm->stack_count];
  }
  return 1;
}

static int ms_vm_peek(const MsVM* vm, size_t distance, MsValue* out_value) {
  size_t index;

  if (vm == NULL || out_value == NULL || distance >= vm->stack_count) {
    return 0;
  }

  index = vm->stack_count - distance - 1;
  *out_value = vm->stack[index];
  return 1;
}

static int ms_vm_append_runtime_error(MsVM* vm,
                                      size_t instruction_offset,
                                      const char* code,
                                      const char* message) {
  MsDiagnostic diagnostic;
  MsCallFrame* frame;
  int line = 0;
  const char* file = "<chunk>";

  if (vm == NULL) {
    return 0;
  }

  frame = ms_vm_current_frame(vm);
  if (frame == NULL || frame->chunk == NULL) {
    return 0;
  }

  if (!ms_chunk_get_line(frame->chunk, instruction_offset, &line) &&
      instruction_offset > 0) {
    ms_chunk_get_line(frame->chunk, instruction_offset - 1, &line);
  }
  if (vm->current_module != NULL && vm->current_module->name != NULL) {
    file = vm->current_module->name;
  }

  diagnostic.phase = "runtime";
  diagnostic.code = code;
  diagnostic.message = message;
  diagnostic.span.file = file;
  diagnostic.span.line = line;
  diagnostic.span.column = 1;
  diagnostic.span.length = 1;
  return ms_diag_list_append(&vm->diagnostics, &diagnostic);
}

static MsVmResult ms_vm_runtime_error(MsVM* vm,
                                      size_t instruction_offset,
                                      const char* code,
                                      const char* message) {
  ms_vm_append_runtime_error(vm, instruction_offset, code, message);
  if (vm != NULL) {
    vm->stack_count = 0;
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
  }
  return MS_VM_RESULT_RUNTIME_ERROR;
}

static int ms_vm_read_byte(MsVM* vm,
                           MsCallFrame* frame,
                           size_t instruction_offset,
                           uint8_t* out_byte) {
  if (vm == NULL || frame == NULL || frame->chunk == NULL || out_byte == NULL) {
    return 0;
  }
  if (!ms_chunk_read_byte(frame->chunk, frame->ip, out_byte)) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4001",
                        "invalid opcode stream");
    return 0;
  }

  frame->ip += 1;
  return 1;
}

static int ms_vm_read_short(MsVM* vm,
                            MsCallFrame* frame,
                            size_t instruction_offset,
                            uint16_t* out_value) {
  if (vm == NULL || frame == NULL || frame->chunk == NULL || out_value == NULL) {
    return 0;
  }
  if (!ms_chunk_read_short(frame->chunk, frame->ip, out_value)) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4001",
                        "invalid opcode stream");
    return 0;
  }

  frame->ip += 2;
  return 1;
}

static int ms_vm_get_string_constant(MsVM* vm,
                                     MsCallFrame* frame,
                                     size_t instruction_offset,
                                     uint8_t constant_index,
                                     MsString** out_name) {
  MsValue constant = ms_value_nil();

  if (vm == NULL || frame == NULL || out_name == NULL || frame->chunk == NULL) {
    return 0;
  }
  if (!ms_chunk_get_constant(frame->chunk, constant_index, &constant)) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4001",
                        "invalid opcode stream");
    return 0;
  }
  if (!ms_value_get_string(constant, out_name)) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4004",
                        "invalid global name");
    return 0;
  }

  return 1;
}

static int ms_vm_get_function_constant(MsVM* vm,
                                       MsCallFrame* frame,
                                       size_t instruction_offset,
                                       uint8_t constant_index,
                                       MsFunction** out_function) {
  MsValue constant = ms_value_nil();

  if (vm == NULL || frame == NULL || out_function == NULL || frame->chunk == NULL) {
    return 0;
  }
  if (!ms_chunk_get_constant(frame->chunk, constant_index, &constant)) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4001",
                        "invalid opcode stream");
    return 0;
  }
  if (!ms_value_get_function(constant, out_function)) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4001",
                        "invalid closure constant");
    return 0;
  }

  return 1;
}

static int ms_vm_require_module(MsVM* vm, size_t instruction_offset) {
  if (vm != NULL && vm->current_module != NULL) {
    return 1;
  }

  if (vm != NULL) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4004",
                        "missing current module");
  }
  return 0;
}

static int ms_vm_write_text(MsVM* vm, const char* text, size_t length) {
  if (vm == NULL || text == NULL) {
    return 0;
  }
  if (vm->write_fn != NULL) {
    return vm->write_fn(vm->write_user_data, text, length);
  }

  return fwrite(text, 1, length, stdout) == length;
}

static int ms_vm_print_value(MsVM* vm, MsValue value) {
  MsString* string = NULL;
  char text[128];

  if (ms_value_get_string(value, &string)) {
    return ms_vm_write_text(vm, string->bytes, string->length) &&
           ms_vm_write_text(vm, "\n", 1);
  }
  if (!ms_value_format(value, text, sizeof(text))) {
    return 0;
  }

  return ms_vm_write_text(vm, text, strlen(text)) &&
         ms_vm_write_text(vm, "\n", 1);
}

static MsVmResult ms_vm_binary_number_op(MsVM* vm,
                                         size_t instruction_offset,
                                         MsOpcode opcode) {
  MsValue left;
  MsValue right;
  double left_number = 0.0;
  double right_number = 0.0;

  if (!ms_vm_peek(vm, 0, &right) || !ms_vm_peek(vm, 1, &left)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }
  if (!ms_value_get_number(left, &left_number) ||
      !ms_value_get_number(right, &right_number)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4003",
                               "operands must be numbers");
  }

  vm->stack_count -= 2;
  switch (opcode) {
    case MS_OP_ADD:
      if (!ms_vm_push(vm, ms_value_number(left_number + right_number))) {
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
      }
      return MS_VM_RESULT_OK;
    case MS_OP_SUBTRACT:
      if (!ms_vm_push(vm, ms_value_number(left_number - right_number))) {
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
      }
      return MS_VM_RESULT_OK;
    case MS_OP_MULTIPLY:
      if (!ms_vm_push(vm, ms_value_number(left_number * right_number))) {
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
      }
      return MS_VM_RESULT_OK;
    case MS_OP_DIVIDE:
      if (!ms_vm_push(vm, ms_value_number(left_number / right_number))) {
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
      }
      return MS_VM_RESULT_OK;
    case MS_OP_GREATER:
      if (!ms_vm_push(vm, ms_value_bool(left_number > right_number))) {
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
      }
      return MS_VM_RESULT_OK;
    case MS_OP_LESS:
      if (!ms_vm_push(vm, ms_value_bool(left_number < right_number))) {
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
      }
      return MS_VM_RESULT_OK;
    default:
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4001",
                                 "invalid opcode stream");
  }
}

static MsUpvalue* ms_vm_capture_upvalue(MsVM* vm, MsValue* location) {
  MsUpvalue* previous = NULL;
  MsUpvalue* current;
  MsUpvalue* created;

  if (vm == NULL || location == NULL) {
    return NULL;
  }

  current = vm->open_upvalues;
  while (current != NULL && current->location > location) {
    previous = current;
    current = current->next;
  }
  if (current != NULL && current->location == location) {
    return current;
  }

  created = ms_upvalue_new(location);
  if (created == NULL) {
    return NULL;
  }
  created->next = current;

  if (previous == NULL) {
    vm->open_upvalues = created;
  } else {
    previous->next = created;
  }

  return created;
}

static void ms_vm_close_upvalues(MsVM* vm, MsValue* last) {
  if (vm == NULL || last == NULL) {
    return;
  }

  while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
    MsUpvalue* upvalue = vm->open_upvalues;

    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm->open_upvalues = upvalue->next;
  }
}
static MsVmResult ms_vm_call_closure(MsVM* vm,
                                     MsClosure* closure,
                                     int argc,
                                     size_t instruction_offset) {
  size_t callee_index;
  size_t i;
  MsCallFrame* frame;

  if (vm == NULL || closure == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (argc != closure->function->arity) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4007",
                               "wrong number of arguments");
  }
  if (!ms_vm_ensure_frames(vm, vm->frame_count + 1)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  callee_index = vm->stack_count - (size_t) argc - 1;
  for (i = 0; i < (size_t) argc; ++i) {
    vm->stack[callee_index + i] = vm->stack[callee_index + i + 1];
  }
  vm->stack_count -= 1;

  frame = &vm->frames[vm->frame_count];
  frame->chunk = &closure->function->chunk;
  frame->closure = closure;
  frame->ip = 0;
  frame->stack_base = callee_index;
  frame->receiver = ms_value_nil();
  frame->has_receiver = 0;
  vm->frame_count += 1;
  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_call_bound_method(MsVM* vm,
                                          MsClosure* method,
                                          MsValue receiver,
                                          int argc,
                                          size_t instruction_offset) {
  size_t callee_index;
  MsCallFrame* frame;

  if (vm == NULL || method == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (argc != method->function->arity) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4007",
                               "wrong number of arguments");
  }
  if ((method->function->flags & MS_FUNCTION_FLAG_HAS_SUPER) != 0) {
    MsClass* owner_class = method->owner_class;

    if (owner_class == NULL || owner_class->superclass == NULL ||
        !ms_vm_ensure_stack(vm, vm->stack_count + 1)) {
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4001",
                                 "invalid opcode stream");
    }
  }
  if (!ms_vm_ensure_frames(vm, vm->frame_count + 1)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  callee_index = vm->stack_count - (size_t) argc - 1;
  if ((method->function->flags & MS_FUNCTION_FLAG_HAS_SUPER) != 0) {
    size_t i;
    MsValue superclass = ms_value_object((MsObject*) method->owner_class->superclass);

    for (i = vm->stack_count; i > callee_index + 1; --i) {
      vm->stack[i] = vm->stack[i - 1];
    }
    vm->stack[callee_index] = receiver;
    vm->stack[callee_index + 1] = superclass;
    vm->stack_count += 1;
  } else {
    vm->stack[callee_index] = receiver;
  }

  frame = &vm->frames[vm->frame_count];
  frame->chunk = &method->function->chunk;
  frame->closure = method;
  frame->ip = 0;
  frame->stack_base = callee_index;
  frame->receiver = receiver;
  frame->has_receiver = 1;
  vm->frame_count += 1;
  return MS_VM_RESULT_OK;
}

static int ms_vm_lookup_method(MsClass* klass,
                               MsString* name,
                               MsValue* out_value,
                               int* out_found) {
  MsValue value = ms_value_nil();
  int found = 0;

  if (out_value == NULL || out_found == NULL) {
    return 0;
  }

  while (klass != NULL) {
    if (!ms_table_get(&klass->methods, name, &value, &found)) {
      return 0;
    }
    if (found) {
      *out_value = value;
      *out_found = 1;
      return 1;
    }
    klass = klass->superclass;
  }

  *out_value = ms_value_nil();
  *out_found = 0;
  return 1;
}

static MsVmResult ms_vm_call_class(MsVM* vm,
                                   MsClass* klass,
                                   int argc,
                                   size_t instruction_offset) {
  size_t callee_index;
  MsInstance* instance;
  MsString* init_name = NULL;
  MsValue init_value = ms_value_nil();
  MsClosure* init_method = NULL;
  int found = 0;

  if (vm == NULL || klass == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }

  callee_index = vm->stack_count - (size_t) argc - 1;
  instance = ms_instance_new(klass);
  if (instance == NULL) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  vm->stack[callee_index] = ms_value_object((MsObject*) instance);

  init_name = ms_string_from_cstr("init");
  if (init_name == NULL) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  if (!ms_vm_lookup_method(klass, init_name, &init_value, &found)) {
    ms_string_free(init_name);
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  ms_string_free(init_name);

  if (!found) {
    if (argc != 0) {
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4007",
                                 "wrong number of arguments");
    }
    return MS_VM_RESULT_OK;
  }
  if (!ms_value_get_closure(init_value, &init_method)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return ms_vm_call_bound_method(vm,
                                 init_method,
                                 ms_value_object((MsObject*) instance),
                                 argc,
                                 instruction_offset);
}

static MsVmResult ms_vm_call_native(MsVM* vm,
                                    MsNativeFunction* native_function,
                                    int argc,
                                    size_t instruction_offset) {
  size_t callee_index;
  MsCallResult result;

  if (vm == NULL || native_function == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (argc != native_function->arity) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4007",
                               "wrong number of arguments");
  }

  callee_index = vm->stack_count - (size_t) argc - 1;
  result = native_function->function(vm, argc, vm->stack + callee_index + 1);
  if (!result.ok) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4008",
                               "native call failed");
  }

  vm->stack_count = callee_index;
  if (!ms_vm_push(vm, result.value)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_call_value(MsVM* vm,
                                   MsValue callee,
                                   int argc,
                                   size_t instruction_offset) {
  MsClosure* closure = NULL;
  MsBoundMethod* bound_method = NULL;
  MsNativeFunction* native_function = NULL;
  MsClass* klass = NULL;

  if (ms_value_get_closure(callee, &closure)) {
    return ms_vm_call_closure(vm, closure, argc, instruction_offset);
  }
  if (ms_value_get_bound_method(callee, &bound_method)) {
    return ms_vm_call_bound_method(vm,
                                   bound_method->method,
                                   bound_method->receiver,
                                   argc,
                                   instruction_offset);
  }
  if (ms_value_get_native_function(callee, &native_function)) {
    return ms_vm_call_native(vm, native_function, argc, instruction_offset);
  }
  if (ms_value_get_class(callee, &klass)) {
    return ms_vm_call_class(vm, klass, argc, instruction_offset);
  }

  return ms_vm_runtime_error(vm,
                             instruction_offset,
                             "MS4006",
                             "value is not callable");
}

static MsVmResult ms_vm_push_class(MsVM* vm,
                                   MsString* name,
                                   size_t instruction_offset) {
  MsClass* klass;

  if (vm == NULL || name == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }

  klass = ms_class_new(name->bytes, name->length, NULL);
  if (klass == NULL) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  if (!ms_vm_push(vm, ms_value_object((MsObject*) klass))) {
    ms_class_free(klass);
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_define_method(MsVM* vm,
                                      MsString* name,
                                      size_t instruction_offset) {
  MsValue method_value = ms_value_nil();
  MsValue class_value = ms_value_nil();
  MsClass* klass = NULL;
  MsClosure* method = NULL;
  int inserted_new = 0;

  if (vm == NULL || name == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (!ms_vm_peek(vm, 0, &method_value) || !ms_vm_peek(vm, 1, &class_value)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }
  if (!ms_value_get_class(class_value, &klass) ||
      !ms_value_get_closure(method_value, &method) ||
      !ms_table_set(&klass->methods, name, method_value, &inserted_new)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  method->owner_class = klass;
  if (!ms_vm_pop(vm, NULL)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }

  return MS_VM_RESULT_OK;
}
static MsVmResult ms_vm_inherit(MsVM* vm, size_t instruction_offset) {
  MsValue superclass_value = ms_value_nil();
  MsValue subclass_value = ms_value_nil();
  MsClass* superclass = NULL;
  MsClass* subclass = NULL;

  if (vm == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (!ms_vm_peek(vm, 0, &superclass_value) || !ms_vm_peek(vm, 1, &subclass_value)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }
  if (!ms_value_get_class(superclass_value, &superclass) ||
      !ms_value_get_class(subclass_value, &subclass)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4009",
                               "superclass must be a class");
  }

  subclass->superclass = superclass;
  if (!ms_vm_pop(vm, NULL)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_get_super(MsVM* vm,
                                  MsString* name,
                                  size_t instruction_offset) {
  MsCallFrame* frame;
  MsValue superclass_value = ms_value_nil();
  MsValue method_value = ms_value_nil();
  MsClass* superclass = NULL;
  MsClosure* method = NULL;
  MsBoundMethod* bound_method = NULL;
  int found = 0;

  if (vm == NULL || name == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  frame = ms_vm_current_frame(vm);
  if (frame == NULL || !frame->has_receiver) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  if (!ms_vm_peek(vm, 0, &superclass_value)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }
  if (!ms_value_get_class(superclass_value, &superclass)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  if (!ms_vm_lookup_method(superclass, name, &method_value, &found) || !found ||
      !ms_value_get_closure(method_value, &method)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               found ? "MS4001" : "MS4010",
                               found ? "invalid opcode stream" : "undefined property");
  }

  bound_method = ms_bound_method_new(frame->receiver, method);
  if (bound_method == NULL) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  vm->stack_count -= 1;
  if (!ms_vm_push(vm, ms_value_object((MsObject*) bound_method))) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_get_property(MsVM* vm,
                                     MsString* name,
                                     size_t instruction_offset) {
  MsValue receiver = ms_value_nil();
  MsValue value = ms_value_nil();
  MsInstance* instance = NULL;
  MsClosure* method = NULL;
  MsBoundMethod* bound_method = NULL;
  int found = 0;

  if (vm == NULL || name == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (!ms_vm_peek(vm, 0, &receiver)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }
  if (!ms_value_get_instance(receiver, &instance)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4009",
                               "only instances have properties");
  }
  if (!ms_table_get(&instance->fields, name, &value, &found)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }
  if (!found) {
    if (!ms_vm_lookup_method(instance->klass, name, &value, &found)) {
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4001",
                                 "invalid opcode stream");
    }
    if (found) {
      if (!ms_value_get_closure(value, &method)) {
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
      }
      bound_method = ms_bound_method_new(receiver, method);
      if (bound_method == NULL) {
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
      }
      value = ms_value_object((MsObject*) bound_method);
    }
  }
  if (!found) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4010",
                               "undefined property");
  }

  vm->stack_count -= 1;
  if (!ms_vm_push(vm, value)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_set_property(MsVM* vm,
                                     MsString* name,
                                     size_t instruction_offset) {
  MsValue receiver = ms_value_nil();
  MsValue value = ms_value_nil();
  MsInstance* instance = NULL;
  int inserted_new = 0;

  if (vm == NULL || name == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (!ms_vm_peek(vm, 0, &value) || !ms_vm_peek(vm, 1, &receiver)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }
  if (!ms_value_get_instance(receiver, &instance)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4009",
                               "only instances have properties");
  }
  if (!ms_table_set(&instance->fields, name, value, &inserted_new)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  vm->stack_count -= 2;
  if (!ms_vm_push(vm, value)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_build_list(MsVM* vm,
                                   uint8_t element_count,
                                   size_t instruction_offset) {
  MsList* list = NULL;
  size_t start;
  size_t i;

  if (vm == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (vm->stack_count < (size_t) element_count) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }

  list = ms_list_new();
  if (list == NULL) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  start = vm->stack_count - (size_t) element_count;
  for (i = start; i < vm->stack_count; ++i) {
    if (!ms_value_array_append(&list->elements, vm->stack[i])) {
      ms_list_free(list);
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4001",
                                 "invalid opcode stream");
    }
  }

  vm->stack_count = start;
  if (!ms_vm_push(vm, ms_value_object((MsObject*) list))) {
    ms_list_free(list);
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_build_tuple(MsVM* vm,
                                    uint8_t element_count,
                                    size_t instruction_offset) {
  MsTuple* tuple = NULL;
  size_t start;
  size_t i;

  if (vm == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (vm->stack_count < (size_t) element_count) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }

  tuple = ms_tuple_new();
  if (tuple == NULL) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  start = vm->stack_count - (size_t) element_count;
  for (i = start; i < vm->stack_count; ++i) {
    if (!ms_value_array_append(&tuple->elements, vm->stack[i])) {
      ms_tuple_free(tuple);
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4001",
                                 "invalid opcode stream");
    }
  }

  vm->stack_count = start;
  if (!ms_vm_push(vm, ms_value_object((MsObject*) tuple))) {
    ms_tuple_free(tuple);
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_build_map(MsVM* vm,
                                  uint8_t entry_count,
                                  size_t instruction_offset) {
  MsMap* map = NULL;
  size_t required_count;
  size_t start;
  size_t i;

  if (vm == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }

  required_count = (size_t) entry_count * 2;
  if (vm->stack_count < required_count) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }

  map = ms_map_new();
  if (map == NULL) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  start = vm->stack_count - required_count;
  for (i = start; i < vm->stack_count; i += 2) {
    MsString* key = NULL;
    MsValue entry_value = vm->stack[i + 1];
    int inserted_new = 0;

    if (!ms_value_get_string(vm->stack[i], &key)) {
      ms_map_free(map);
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4011",
                                 "map keys must be strings");
    }
    if (!ms_table_set(map->entries, key, entry_value, &inserted_new)) {
      ms_map_free(map);
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4001",
                                 "invalid opcode stream");
    }
  }

  vm->stack_count = start;
  if (!ms_vm_push(vm, ms_value_object((MsObject*) map))) {
    ms_map_free(map);
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_resolve_sequence_index(MsVM* vm,
                                               MsValue index_value,
                                               size_t length,
                                               size_t instruction_offset,
                                               size_t* out_index) {
  double number = 0.0;
  size_t index;

  if (out_index == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (!ms_value_get_number(index_value, &number) || number < 0.0) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4013",
                               "index must be a non-negative integer");
  }

  index = (size_t) number;
  if ((double) index != number) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4013",
                               "index must be a non-negative integer");
  }
  if (index >= length) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4014",
                               "index out of range");
  }

  *out_index = index;
  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_index_get(MsVM* vm, size_t instruction_offset) {
  MsValue receiver = ms_value_nil();
  MsValue index_value = ms_value_nil();
  MsValue result = ms_value_nil();
  MsList* list = NULL;
  MsTuple* tuple = NULL;
  size_t index = 0;

  if (vm == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (!ms_vm_peek(vm, 0, &index_value) || !ms_vm_peek(vm, 1, &receiver)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }

  if (ms_value_get_list(receiver, &list)) {
    if (ms_vm_resolve_sequence_index(vm,
                                     index_value,
                                     list->elements.count,
                                     instruction_offset,
                                     &index) != MS_VM_RESULT_OK) {
      return MS_VM_RESULT_RUNTIME_ERROR;
    }
    result = list->elements.items[index];
  } else if (ms_value_get_tuple(receiver, &tuple)) {
    if (ms_vm_resolve_sequence_index(vm,
                                     index_value,
                                     tuple->elements.count,
                                     instruction_offset,
                                     &index) != MS_VM_RESULT_OK) {
      return MS_VM_RESULT_RUNTIME_ERROR;
    }
    result = tuple->elements.items[index];
  } else {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4012",
                               "value is not indexable");
  }

  vm->stack_count -= 2;
  if (!ms_vm_push(vm, result)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

static MsVmResult ms_vm_index_set(MsVM* vm, size_t instruction_offset) {
  MsValue receiver = ms_value_nil();
  MsValue index_value = ms_value_nil();
  MsValue value = ms_value_nil();
  MsList* list = NULL;
  MsTuple* tuple = NULL;
  size_t index = 0;

  if (vm == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  if (!ms_vm_peek(vm, 0, &value) ||
      !ms_vm_peek(vm, 1, &index_value) ||
      !ms_vm_peek(vm, 2, &receiver)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4002",
                               "stack underflow");
  }

  if (ms_value_get_list(receiver, &list)) {
    if (ms_vm_resolve_sequence_index(vm,
                                     index_value,
                                     list->elements.count,
                                     instruction_offset,
                                     &index) != MS_VM_RESULT_OK) {
      return MS_VM_RESULT_RUNTIME_ERROR;
    }
    list->elements.items[index] = value;
  } else if (ms_value_get_tuple(receiver, &tuple)) {
    (void) tuple;
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4015",
                               "tuple elements are immutable");
  } else {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4012",
                               "value is not indexable");
  }

  vm->stack_count -= 3;
  if (!ms_vm_push(vm, value)) {
    return ms_vm_runtime_error(vm,
                               instruction_offset,
                               "MS4001",
                               "invalid opcode stream");
  }

  return MS_VM_RESULT_OK;
}

void ms_module_init(MsModule* module, const char* name) {
  if (module == NULL) {
    return;
  }

  module->name = name;
  ms_table_init(&module->globals);
}

void ms_module_destroy(MsModule* module) {
  if (module == NULL) {
    return;
  }

  ms_table_destroy(&module->globals);
  module->name = NULL;
}

void ms_vm_init(MsVM* vm) {
  if (vm == NULL) {
    return;
  }

  vm->stack = NULL;
  vm->stack_count = 0;
  vm->stack_capacity = 0;
  vm->frames = NULL;
  vm->frame_count = 0;
  vm->frame_capacity = 0;
  vm->open_upvalues = NULL;
  vm->current_module = NULL;
  ms_diag_list_init(&vm->diagnostics);
  vm->write_fn = NULL;
  vm->write_user_data = NULL;
}

void ms_vm_destroy(MsVM* vm) {
  if (vm == NULL) {
    return;
  }

  free(vm->frames);
  free(vm->stack);
  vm->frames = NULL;
  vm->frame_count = 0;
  vm->frame_capacity = 0;
  vm->stack = NULL;
  vm->stack_count = 0;
  vm->stack_capacity = 0;
  vm->open_upvalues = NULL;
  vm->current_module = NULL;
  ms_diag_list_destroy(&vm->diagnostics);
  vm->write_fn = NULL;
  vm->write_user_data = NULL;
}

void ms_vm_set_current_module(MsVM* vm, MsModule* module) {
  if (vm == NULL) {
    return;
  }

  vm->current_module = module;
}

void ms_vm_set_write_callback(MsVM* vm,
                              MsVmWriteFn write_fn,
                              void* write_user_data) {
  if (vm == NULL) {
    return;
  }

  vm->write_fn = write_fn;
  vm->write_user_data = write_user_data;
}

int ms_vm_define_native(MsVM* vm,
                        MsModule* module,
                        const char* name,
                        int arity,
                        MsNativeFn function) {
  MsModule* target_module;
  MsString* key;
  MsNativeFunction* native_function;
  int inserted_new = 0;

  target_module = module;
  if (target_module == NULL && vm != NULL) {
    target_module = vm->current_module;
  }
  if (target_module == NULL || name == NULL || function == NULL) {
    return 0;
  }

  key = ms_string_from_cstr(name);
  native_function = ms_native_function_new(name, strlen(name), arity, function);
  if (key == NULL || native_function == NULL) {
    ms_string_free(key);
    ms_native_function_free(native_function);
    return 0;
  }

  return ms_table_set(&target_module->globals,
                      key,
                      ms_value_object((MsObject*) native_function),
                      &inserted_new);
}

MsVmResult ms_vm_run_chunk(MsVM* vm, const MsChunk* chunk) {
  if (vm == NULL || chunk == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }

  ms_vm_reset(vm);
  if (!ms_vm_ensure_frames(vm, 1)) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }
  vm->frames[0].chunk = chunk;
  vm->frames[0].closure = NULL;
  vm->frames[0].ip = 0;
  vm->frames[0].stack_base = 0;
  vm->frames[0].receiver = ms_value_nil();
  vm->frames[0].has_receiver = 0;
  vm->frame_count = 1;

  while (vm->frame_count > 0) {
    MsCallFrame* frame = ms_vm_current_frame(vm);
    MsValue value = ms_value_nil();
    MsValue left = ms_value_nil();
    MsValue right = ms_value_nil();
    uint8_t opcode = 0;
    uint8_t operand = 0;
    uint8_t operand2 = 0;
    uint16_t jump = 0;
    size_t instruction_offset;
    MsString* name = NULL;
    MsFunction* function = NULL;
    MsClosure* closure = NULL;
    MsValue global_value = ms_value_nil();
    int found = 0;
    int inserted_new = 0;
    double number = 0.0;

    if (frame == NULL || frame->chunk == NULL) {
      return MS_VM_RESULT_RUNTIME_ERROR;
    }

    instruction_offset = frame->ip;
    if (!ms_chunk_read_byte(frame->chunk, frame->ip, &opcode)) {
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4001",
                                 "invalid opcode stream");
    }
    frame->ip += 1;

    switch ((MsOpcode) opcode) {
      case MS_OP_CONSTANT:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_chunk_get_constant(frame->chunk, operand, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        if (!ms_vm_push(vm, value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_NIL:
        if (!ms_vm_push(vm, ms_value_nil())) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_TRUE:
        if (!ms_vm_push(vm, ms_value_bool(1))) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_FALSE:
        if (!ms_vm_push(vm, ms_value_bool(0))) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_POP:
        if (!ms_vm_pop(vm, NULL)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        break;
      case MS_OP_GET_LOCAL:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (frame->stack_base + operand >= vm->stack_count) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (!ms_vm_push(vm, vm->stack[frame->stack_base + operand])) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_SET_LOCAL:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_vm_peek(vm, 0, &value) ||
            frame->stack_base + operand >= vm->stack_count) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        vm->stack[frame->stack_base + operand] = value;
        break;
      case MS_OP_GET_UPVALUE:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            frame->closure == NULL ||
            operand >= frame->closure->upvalue_count ||
            frame->closure->upvalues[operand] == NULL) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        if (!ms_vm_push(vm, *frame->closure->upvalues[operand]->location)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_SET_UPVALUE:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            frame->closure == NULL ||
            operand >= frame->closure->upvalue_count ||
            frame->closure->upvalues[operand] == NULL ||
            !ms_vm_peek(vm, 0, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        *frame->closure->upvalues[operand]->location = value;
        break;      case MS_OP_GET_GLOBAL:
        if (!ms_vm_require_module(vm, instruction_offset) ||
            !ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, frame, instruction_offset, operand, &name)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_table_get(&vm->current_module->globals,
                          name,
                          &global_value,
                          &found) ||
            !found) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4004",
                                     "undefined global");
        }
        if (!ms_vm_push(vm, global_value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_DEFINE_GLOBAL:
        if (!ms_vm_require_module(vm, instruction_offset) ||
            !ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, frame, instruction_offset, operand, &name)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_vm_pop(vm, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (!ms_table_set(&vm->current_module->globals,
                          name,
                          value,
                          &inserted_new)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_SET_GLOBAL:
        if (!ms_vm_require_module(vm, instruction_offset) ||
            !ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, frame, instruction_offset, operand, &name)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_vm_peek(vm, 0, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (!ms_table_get(&vm->current_module->globals,
                          name,
                          &global_value,
                          &found) ||
            !found) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4004",
                                     "undefined global");
        }
        if (!ms_table_set(&vm->current_module->globals,
                          name,
                          value,
                          &inserted_new) ||
            inserted_new) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4004",
                                     "undefined global");
        }
        break;
      case MS_OP_EQUAL:
        if (!ms_vm_pop(vm, &right) || !ms_vm_pop(vm, &left)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (!ms_vm_push(vm, ms_value_bool(ms_value_equals(left, right)))) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_GREATER:
      case MS_OP_LESS:
      case MS_OP_ADD:
      case MS_OP_SUBTRACT:
      case MS_OP_MULTIPLY:
      case MS_OP_DIVIDE:
        if (ms_vm_binary_number_op(vm,
                                   instruction_offset,
                                   (MsOpcode) opcode) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_NOT:
        if (!ms_vm_pop(vm, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (!ms_vm_push(vm, ms_value_bool(ms_value_is_falsey(value)))) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_NEGATE:
        if (!ms_vm_pop(vm, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (!ms_value_get_number(value, &number)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4003",
                                     "operands must be numbers");
        }
        if (!ms_vm_push(vm, ms_value_number(-number))) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_PRINT:
        if (!ms_vm_pop(vm, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (!ms_vm_print_value(vm, value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_JUMP:
        if (!ms_vm_read_short(vm, frame, instruction_offset, &jump)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (frame->ip + (size_t) jump >= ms_chunk_code_count(frame->chunk)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4005",
                                     "invalid jump target");
        }
        frame->ip += (size_t) jump;
        break;
      case MS_OP_JUMP_IF_FALSE:
        if (!ms_vm_read_short(vm, frame, instruction_offset, &jump)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_vm_peek(vm, 0, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (ms_value_is_falsey(value)) {
          if (frame->ip + (size_t) jump >= ms_chunk_code_count(frame->chunk)) {
            return ms_vm_runtime_error(vm,
                                       instruction_offset,
                                       "MS4005",
                                       "invalid jump target");
          }
          frame->ip += (size_t) jump;
        }
        break;
      case MS_OP_LOOP:
        if (!ms_vm_read_short(vm, frame, instruction_offset, &jump)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if ((size_t) jump > frame->ip) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4005",
                                     "invalid jump target");
        }
        frame->ip -= (size_t) jump;
        if (frame->ip >= ms_chunk_code_count(frame->chunk)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4005",
                                     "invalid jump target");
        }
        break;
      case MS_OP_CALL:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_peek(vm, (size_t) operand, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (ms_vm_call_value(vm, value, (int) operand, instruction_offset) !=
            MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_CLASS:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, frame, instruction_offset, operand, &name)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (ms_vm_push_class(vm, name, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_INHERIT:
        if (ms_vm_inherit(vm, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_METHOD:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, frame, instruction_offset, operand, &name)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (ms_vm_define_method(vm, name, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_GET_PROPERTY:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, frame, instruction_offset, operand, &name)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (ms_vm_get_property(vm, name, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_SET_PROPERTY:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, frame, instruction_offset, operand, &name)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (ms_vm_set_property(vm, name, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_GET_SUPER:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, frame, instruction_offset, operand, &name)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (ms_vm_get_super(vm, name, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_BUILD_LIST:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (ms_vm_build_list(vm, operand, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_BUILD_TUPLE:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (ms_vm_build_tuple(vm, operand, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_BUILD_MAP:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (ms_vm_build_map(vm, operand, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_INDEX_GET:
        if (ms_vm_index_get(vm, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_INDEX_SET:
        if (ms_vm_index_set(vm, instruction_offset) != MS_VM_RESULT_OK) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        break;
      case MS_OP_CLOSURE:
        if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand) ||
            !ms_vm_get_function_constant(vm, frame, instruction_offset, operand,
                                         &function)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        closure = ms_closure_new(function);
        if (closure == NULL) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        for (operand = 0; operand < closure->upvalue_count; ++operand) {
          uint8_t slot = 0;

          if (!ms_vm_read_byte(vm, frame, instruction_offset, &operand2) ||
              !ms_vm_read_byte(vm, frame, instruction_offset, &slot)) {
            ms_closure_free(closure);
            return MS_VM_RESULT_RUNTIME_ERROR;
          }
          if (operand2 != 0) {
            if (frame->stack_base + slot >= vm->stack_count) {
              ms_closure_free(closure);
              return ms_vm_runtime_error(vm,
                                         instruction_offset,
                                         "MS4002",
                                         "stack underflow");
            }
            closure->upvalues[operand] = ms_vm_capture_upvalue(
                vm, &vm->stack[frame->stack_base + slot]);
          } else {
            if (frame->closure == NULL || slot >= frame->closure->upvalue_count) {
              ms_closure_free(closure);
              return ms_vm_runtime_error(vm,
                                         instruction_offset,
                                         "MS4001",
                                         "invalid opcode stream");
            }
            closure->upvalues[operand] = frame->closure->upvalues[slot];
          }
          if (closure->upvalues[operand] == NULL) {
            ms_closure_free(closure);
            return ms_vm_runtime_error(vm,
                                       instruction_offset,
                                       "MS4001",
                                       "invalid opcode stream");
          }
        }
        if (!ms_vm_push(vm, ms_value_object((MsObject*) closure))) {
          ms_closure_free(closure);
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_CLOSE_UPVALUE:
        if (vm->stack_count == 0) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        ms_vm_close_upvalues(vm, &vm->stack[vm->stack_count - 1]);
        vm->stack_count -= 1;
        break;
      case MS_OP_RETURN:
        if (!ms_vm_pop(vm, &value)) {
          value = ms_value_nil();
        }
        if (vm->stack != NULL) {
          ms_vm_close_upvalues(vm, &vm->stack[frame->stack_base]);
        }
        vm->stack_count = frame->stack_base;
        vm->frame_count -= 1;
        if (vm->frame_count == 0) {
          vm->stack_count = 0;
          return MS_VM_RESULT_OK;
        }
        if (!ms_vm_push(vm, value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      default:
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
    }
  }

  return MS_VM_RESULT_OK;
}
