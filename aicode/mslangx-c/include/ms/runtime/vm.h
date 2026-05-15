#ifndef MSLANGC_RUNTIME_VM_H_
#define MSLANGC_RUNTIME_VM_H_

#include <stddef.h>

#include "ms/cache/source_loader.h"
#include "ms/diag.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/function.h"
#include "ms/table.h"

typedef enum MsModuleState {
  MS_MODULE_STATE_UNSEEN,
  MS_MODULE_STATE_INITIALIZING,
  MS_MODULE_STATE_INITIALIZED,
  MS_MODULE_STATE_FAILED
} MsModuleState;

typedef struct MsModule {
  MsObject object;
  char* name;
  char* canonical_path;
  MsModuleState state;
  MsTable globals;
} MsModule;

typedef struct MsModuleCache {
  size_t count;
  size_t capacity;
  MsModule** modules;
} MsModuleCache;

typedef struct MsGCState {
  MsObject* objects;
  size_t allocation_count;
  size_t free_count;
  size_t collection_count;
} MsGCState;

typedef struct MsGCTemporaryRoot {
  MsObject* object;
  struct MsGCTemporaryRoot* next;
} MsGCTemporaryRoot;

typedef int (*MsVmWriteFn)(void* user_data, const char* text, size_t length);
typedef MsSourceLoadStatus (*MsVmSourceLoadFn)(void* user_data,
                                               const char* source_path,
                                               const MsSourceLoadOptions* options,
                                               MsDiagnosticList* diagnostics,
                                               MsSourceLoadResult* out_result);

typedef enum MsVmResult {
  MS_VM_RESULT_OK,
  MS_VM_RESULT_RUNTIME_ERROR
} MsVmResult;

typedef struct MsCallFrame {
  const MsChunk* chunk;
  MsClosure* closure;
  MsModule* module;
  size_t ip;
  size_t stack_base;
  MsValue receiver;
  int has_receiver;
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
  int cache_enabled;
  MsVmSourceLoadFn source_load_fn;
  void* source_load_user_data;
  char** module_search_roots;
  size_t module_search_root_count;
  size_t module_search_root_capacity;
  MsModuleCache module_cache;
  MsGCState gc;
  MsGCTemporaryRoot* temporary_roots;
  size_t temporary_root_count;
  MsDiagnosticList diagnostics;
  MsVmWriteFn write_fn;
  void* write_user_data;
  int direct_cache_entry;
} MsVM;

void ms_module_init(MsModule* module, const char* name);
void ms_module_destroy(MsModule* module);
int ms_module_transition_state(MsModule* module, MsModuleState new_state);

void ms_vm_init(MsVM* vm);
void ms_vm_destroy(MsVM* vm);
void ms_vm_gc_track_object(MsVM* vm, MsObject* object);
int ms_vm_gc_push_temporary_root(MsVM* vm, MsObject* object);
void ms_vm_gc_pop_temporary_root(MsVM* vm, MsObject* object);
void ms_vm_gc_mark_roots(MsVM* vm);
void ms_vm_gc_collect(MsVM* vm);
void ms_vm_set_current_module(MsVM* vm, MsModule* module);
void ms_vm_set_cache_enabled(MsVM* vm, int cache_enabled);
void ms_vm_set_direct_cache_entry(MsVM* vm, int direct_cache_entry);
void ms_vm_set_source_load_callback(MsVM* vm,
                                    MsVmSourceLoadFn source_load_fn,
                                    void* source_load_user_data);
int ms_module_build_file_path(const char* module_name, char** out_relative_path);
int ms_vm_add_search_root(MsVM* vm, const char* root_path);
int ms_vm_resolve_module_path(const MsVM* vm,
                              const char* module_name,
                              char** out_canonical_path);
MsModule* ms_vm_get_or_create_module(MsVM* vm,
                                     const char* path,
                                     int* out_inserted_new);
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
