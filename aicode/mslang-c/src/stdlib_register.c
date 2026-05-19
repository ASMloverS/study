#include "ms/stdlib_register.h"
#include "ms/module.h"

/* Stub implementations for builtin module init functions.
   These will be replaced by real implementations in later CAPI tasks.
   Each stub simply marks the module as having an empty exports table. */

void ms_module_math_init  (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_os_init    (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_time_init  (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_io_init    (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_buffer_init(MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_hash_init  (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_log_init   (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_net_init   (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_debug_init (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }
void ms_module_gc_init    (MsVM* vm, MsObjModule* mod) { (void)vm; (void)mod; }

void ms_stdlib_register_all(MsVM* vm) {
    ms_vm_register_builtin_module(vm, "math",   ms_module_math_init);
    ms_vm_register_builtin_module(vm, "os",     ms_module_os_init);
    ms_vm_register_builtin_module(vm, "time",   ms_module_time_init);
    ms_vm_register_builtin_module(vm, "io",     ms_module_io_init);
    ms_vm_register_builtin_module(vm, "buffer", ms_module_buffer_init);
    ms_vm_register_builtin_module(vm, "hash",   ms_module_hash_init);
    ms_vm_register_builtin_module(vm, "log",    ms_module_log_init);
    ms_vm_register_builtin_module(vm, "net",    ms_module_net_init);
    ms_vm_register_builtin_module(vm, "debug",  ms_module_debug_init);
    ms_vm_register_builtin_module(vm, "gc",     ms_module_gc_init);
}
