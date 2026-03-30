#include "ms/runtime/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ms/runtime/opcode.h"
#include "ms/string.h"

static int ms_vm_ensure_stack(MsVM *vm, size_t min_capacity) {
  MsValue *stack;
  size_t new_capacity;

  if (vm == NULL) {
    return 0;
  }
  if (min_capacity <= vm->stack_capacity) {
    return 1;
  }

  new_capacity = vm->stack_capacity == 0 ? 16 : vm->stack_capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (SIZE_MAX / 2)) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2;
  }

  stack = (MsValue *) realloc(vm->stack, new_capacity * sizeof(*stack));
  if (stack == NULL) {
    return 0;
  }

  vm->stack = stack;
  vm->stack_capacity = new_capacity;
  return 1;
}

static void ms_vm_reset(MsVM *vm) {
  if (vm == NULL) {
    return;
  }

  vm->stack_count = 0;
  vm->frame.chunk = NULL;
  vm->frame.ip = 0;
  vm->frame.stack_base = 0;
  vm->frame_active = 0;
  ms_diag_list_clear(&vm->diagnostics);
}

static int ms_vm_push(MsVM *vm, MsValue value) {
  if (vm == NULL || !ms_vm_ensure_stack(vm, vm->stack_count + 1)) {
    return 0;
  }

  vm->stack[vm->stack_count] = value;
  vm->stack_count += 1;
  return 1;
}

static int ms_vm_pop(MsVM *vm, MsValue *out_value) {
  if (vm == NULL || vm->stack_count == 0) {
    return 0;
  }

  vm->stack_count -= 1;
  if (out_value != NULL) {
    *out_value = vm->stack[vm->stack_count];
  }
  return 1;
}

static int ms_vm_peek(const MsVM *vm, size_t distance, MsValue *out_value) {
  size_t index;

  if (vm == NULL || out_value == NULL || distance >= vm->stack_count) {
    return 0;
  }

  index = vm->stack_count - distance - 1;
  *out_value = vm->stack[index];
  return 1;
}

static int ms_vm_append_runtime_error(MsVM *vm,
                                      size_t instruction_offset,
                                      const char *code,
                                      const char *message) {
  MsDiagnostic diagnostic;
  int line = 0;
  const char *file = "<chunk>";

  if (vm == NULL || vm->frame.chunk == NULL) {
    return 0;
  }

  if (!ms_chunk_get_line(vm->frame.chunk, instruction_offset, &line) &&
      instruction_offset > 0) {
    ms_chunk_get_line(vm->frame.chunk, instruction_offset - 1, &line);
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

static MsVmResult ms_vm_runtime_error(MsVM *vm,
                                      size_t instruction_offset,
                                      const char *code,
                                      const char *message) {
  ms_vm_append_runtime_error(vm, instruction_offset, code, message);
  vm->stack_count = 0;
  vm->frame_active = 0;
  return MS_VM_RESULT_RUNTIME_ERROR;
}

static int ms_vm_read_byte(MsVM *vm, size_t instruction_offset, uint8_t *out_byte) {
  if (vm == NULL || vm->frame.chunk == NULL || out_byte == NULL) {
    return 0;
  }
  if (!ms_chunk_read_byte(vm->frame.chunk, vm->frame.ip, out_byte)) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4001",
                        "invalid opcode stream");
    return 0;
  }

  vm->frame.ip += 1;
  return 1;
}

static int ms_vm_read_short(MsVM *vm,
                            size_t instruction_offset,
                            uint16_t *out_value) {
  if (vm == NULL || vm->frame.chunk == NULL || out_value == NULL) {
    return 0;
  }
  if (!ms_chunk_read_short(vm->frame.chunk, vm->frame.ip, out_value)) {
    ms_vm_runtime_error(vm,
                        instruction_offset,
                        "MS4001",
                        "invalid opcode stream");
    return 0;
  }

  vm->frame.ip += 2;
  return 1;
}

static int ms_vm_get_string_constant(MsVM *vm,
                                     size_t instruction_offset,
                                     uint8_t constant_index,
                                     MsString **out_name) {
  MsValue constant = ms_value_nil();

  if (vm == NULL || out_name == NULL || vm->frame.chunk == NULL) {
    return 0;
  }
  if (!ms_chunk_get_constant(vm->frame.chunk, constant_index, &constant)) {
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

static int ms_vm_require_module(MsVM *vm, size_t instruction_offset) {
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

static int ms_vm_write_text(MsVM *vm, const char *text, size_t length) {
  if (vm == NULL || text == NULL) {
    return 0;
  }
  if (vm->write_fn != NULL) {
    return vm->write_fn(vm->write_user_data, text, length);
  }

  return fwrite(text, 1, length, stdout) == length;
}

static int ms_vm_print_value(MsVM *vm, MsValue value) {
  MsString *string = NULL;
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

static MsVmResult ms_vm_binary_number_op(MsVM *vm,
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

void ms_module_init(MsModule *module, const char *name) {
  if (module == NULL) {
    return;
  }

  module->name = name;
  ms_table_init(&module->globals);
}

void ms_module_destroy(MsModule *module) {
  if (module == NULL) {
    return;
  }

  ms_table_destroy(&module->globals);
  module->name = NULL;
}

void ms_vm_init(MsVM *vm) {
  if (vm == NULL) {
    return;
  }

  vm->stack = NULL;
  vm->stack_count = 0;
  vm->stack_capacity = 0;
  vm->frame.chunk = NULL;
  vm->frame.ip = 0;
  vm->frame.stack_base = 0;
  vm->frame_active = 0;
  vm->current_module = NULL;
  ms_diag_list_init(&vm->diagnostics);
  vm->write_fn = NULL;
  vm->write_user_data = NULL;
}

void ms_vm_destroy(MsVM *vm) {
  if (vm == NULL) {
    return;
  }

  free(vm->stack);
  vm->stack = NULL;
  vm->stack_count = 0;
  vm->stack_capacity = 0;
  vm->frame.chunk = NULL;
  vm->frame.ip = 0;
  vm->frame.stack_base = 0;
  vm->frame_active = 0;
  vm->current_module = NULL;
  ms_diag_list_destroy(&vm->diagnostics);
  vm->write_fn = NULL;
  vm->write_user_data = NULL;
}

void ms_vm_set_current_module(MsVM *vm, MsModule *module) {
  if (vm == NULL) {
    return;
  }

  vm->current_module = module;
}

void ms_vm_set_write_callback(MsVM *vm,
                              MsVmWriteFn write_fn,
                              void *write_user_data) {
  if (vm == NULL) {
    return;
  }

  vm->write_fn = write_fn;
  vm->write_user_data = write_user_data;
}

MsVmResult ms_vm_run_chunk(MsVM *vm, const MsChunk *chunk) {
  if (vm == NULL || chunk == NULL) {
    return MS_VM_RESULT_RUNTIME_ERROR;
  }

  ms_vm_reset(vm);
  vm->frame.chunk = chunk;
  vm->frame.ip = 0;
  vm->frame.stack_base = 0;
  vm->frame_active = 1;

  while (vm->frame_active) {
    MsValue value = ms_value_nil();
    MsValue left = ms_value_nil();
    MsValue right = ms_value_nil();
    uint8_t opcode = 0;
    uint8_t operand = 0;
    uint16_t jump = 0;
    size_t instruction_offset = vm->frame.ip;
    MsString *name = NULL;
    MsValue global_value = ms_value_nil();
    int found = 0;
    int inserted_new = 0;
    double number = 0.0;

    if (!ms_chunk_read_byte(chunk, vm->frame.ip, &opcode)) {
      return ms_vm_runtime_error(vm,
                                 instruction_offset,
                                 "MS4001",
                                 "invalid opcode stream");
    }
    vm->frame.ip += 1;

    switch ((MsOpcode) opcode) {
      case MS_OP_CONSTANT:
        if (!ms_vm_read_byte(vm, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_chunk_get_constant(chunk, operand, &value)) {
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
        if (!ms_vm_read_byte(vm, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (vm->frame.stack_base + operand >= vm->stack_count) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (!ms_vm_push(vm, vm->stack[vm->frame.stack_base + operand])) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4001",
                                     "invalid opcode stream");
        }
        break;
      case MS_OP_SET_LOCAL:
        if (!ms_vm_read_byte(vm, instruction_offset, &operand)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_vm_peek(vm, 0, &value) ||
            vm->frame.stack_base + operand >= vm->stack_count) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        vm->stack[vm->frame.stack_base + operand] = value;
        break;
      case MS_OP_GET_GLOBAL:
        if (!ms_vm_require_module(vm, instruction_offset) ||
            !ms_vm_read_byte(vm, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, instruction_offset, operand, &name)) {
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
            !ms_vm_read_byte(vm, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, instruction_offset, operand, &name)) {
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
            !ms_vm_read_byte(vm, instruction_offset, &operand) ||
            !ms_vm_get_string_constant(vm, instruction_offset, operand, &name)) {
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
        if (!ms_vm_read_short(vm, instruction_offset, &jump)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (vm->frame.ip + (size_t) jump >= ms_chunk_code_count(chunk)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4005",
                                     "invalid jump target");
        }
        vm->frame.ip += (size_t) jump;
        break;
      case MS_OP_JUMP_IF_FALSE:
        if (!ms_vm_read_short(vm, instruction_offset, &jump)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if (!ms_vm_peek(vm, 0, &value)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4002",
                                     "stack underflow");
        }
        if (ms_value_is_falsey(value)) {
          if (vm->frame.ip + (size_t) jump >= ms_chunk_code_count(chunk)) {
            return ms_vm_runtime_error(vm,
                                       instruction_offset,
                                       "MS4005",
                                       "invalid jump target");
          }
          vm->frame.ip += (size_t) jump;
        }
        break;
      case MS_OP_LOOP:
        if (!ms_vm_read_short(vm, instruction_offset, &jump)) {
          return MS_VM_RESULT_RUNTIME_ERROR;
        }
        if ((size_t) jump > vm->frame.ip) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4005",
                                     "invalid jump target");
        }
        vm->frame.ip -= (size_t) jump;
        if (vm->frame.ip >= ms_chunk_code_count(chunk)) {
          return ms_vm_runtime_error(vm,
                                     instruction_offset,
                                     "MS4005",
                                     "invalid jump target");
        }
        break;
      case MS_OP_RETURN:
        vm->stack_count = 0;
        vm->frame_active = 0;
        return MS_VM_RESULT_OK;
      default:
        return ms_vm_runtime_error(vm,
                                   instruction_offset,
                                   "MS4001",
                                   "invalid opcode stream");
    }
  }

  return MS_VM_RESULT_OK;
}