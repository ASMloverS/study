#ifndef MSLANGC_RUNTIME_VM_H_
#define MSLANGC_RUNTIME_VM_H_

#include <stddef.h>

#include "ms/diag.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/function.h"
#include "ms/table.h"

typedef struct MsModule {
  const char* name;
  MsTable globals;
} MsModule;

typedef int (*MsVmWriteFn)(void* user_data, const char* text, size_t length);

typedef enum MsVmResult {
  MS_VM_RESULT_OK,
  MS_VM_RESULT_RUNTIME_ERROR
} MsVmResult;

typedef struct MsCallFrame {
  const MsChunk* chunk;
  MsClosure* closure;
  size_t ip;
  size_t stack_base;
} MsCallFrame;

typedef struct MsVM {
  MsValue* stack;
  size_t stack_count;
  size_t stack_capacity;
  MsCallFrame* frames;
  size_t frame_count;
  size_t frame_capacity;
  MsUpvalue* open_upvalues;
  MsModule* current_module;
  MsDiagnosticList diagnostics;
  MsVmWriteFn write_fn;
  void* write_user_data;
} MsVM;

void ms_module_init(MsModule* module, const char* name);
void ms_module_destroy(MsModule* module);

void ms_vm_init(MsVM* vm);
void ms_vm_destroy(MsVM* vm);
void ms_vm_set_current_module(MsVM* vm, MsModule* module);
void ms_vm_set_write_callback(MsVM* vm,
                              MsVmWriteFn write_fn,
                              void* write_user_data);
int ms_vm_define_native(MsVM* vm,
                        MsModule* module,
                        const char* name,
                        int arity,
                        MsNativeFn function);
MsVmResult ms_vm_run_chunk(MsVM* vm, const MsChunk* chunk);

#endif