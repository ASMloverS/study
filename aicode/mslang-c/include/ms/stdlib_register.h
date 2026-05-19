#pragma once
#include "ms/vm.h"

/* Register all builtin stdlib modules into the VM registry.
   Called at the end of ms_vm_init.  Each init function is stored as a lazy
   pointer and only invoked when the user first imports that module. */
void ms_stdlib_register_all(MsVM* vm);

/* Per-module init stubs - implemented in src/stdlib/<name>.c (future tasks).
   Declared here so ms_stdlib_register_all can reference them without
   requiring the stdlib source files to exist yet. */
void ms_module_math_init  (MsVM* vm, MsObjModule* mod);
void ms_module_os_init    (MsVM* vm, MsObjModule* mod);
void ms_module_time_init  (MsVM* vm, MsObjModule* mod);
void ms_module_io_init    (MsVM* vm, MsObjModule* mod);
void ms_module_buffer_init(MsVM* vm, MsObjModule* mod);
void ms_module_hash_init  (MsVM* vm, MsObjModule* mod);
void ms_module_log_init   (MsVM* vm, MsObjModule* mod);
void ms_module_net_init   (MsVM* vm, MsObjModule* mod);
void ms_module_debug_init (MsVM* vm, MsObjModule* mod);
void ms_module_gc_init    (MsVM* vm, MsObjModule* mod);
