#include "ms/runtime/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  vm->frame_count += 1;
  return MS_VM_RESULT_OK;
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
  MsNativeFunction* native_function = NULL;

  if (ms_value_get_closure(callee, &closure)) {
    return ms_vm_call_closure(vm, closure, argc, instruction_offset);
  }
  if (ms_value_get_native_function(callee, &native_function)) {
    return ms_vm_call_native(vm, native_function, argc, instruction_offset);
  }

  return ms_vm_runtime_error(vm,
                             instruction_offset,
                             "MS4006",
                             "value is not callable");
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
        break;      case MS_OP_CLOSURE:
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