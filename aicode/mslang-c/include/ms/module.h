#pragma once
#include "ms/vm.h"

/* ---- Builtin module registry ---- */

/* Callback invoked once, lazily, when the user first imports the module.
   Implementations populate mod->exports via ms_table_set. */
typedef void (*MsBuiltinModuleInit)(MsVM* vm, MsObjModule* mod);

struct MsBuiltinModuleEntry_tag {
    const char*         name; /* bare module name, e.g. "math" (static lifetime) */
    MsBuiltinModuleInit init;
};

/* Register a builtin module.  name must have static (or VM-lifetime) storage. */
void ms_vm_register_builtin_module(MsVM* vm, const char* name,
                                   MsBuiltinModuleInit init);

/* Find a registered builtin init callback; returns NULL if not found. */
MsBuiltinModuleInit ms_vm_find_builtin_module(MsVM* vm, const char* name);

/* ---- Path & file helpers ---- */

/* Resolve import_path relative to from_dir into an absolute canonical path.
   Appends ".ms" if no extension is present. Returns heap-allocated string;
   caller must free(). Returns NULL on failure. */
char* ms_resolve_path(const char* import_path, const char* from_dir);

/* Read the entire contents of path into a heap-allocated buffer.
   Caller must free(). Returns NULL on failure. */
char* ms_read_file(const char* path);

/* Load and execute a module by import_path relative to the file at from_path.
   Returns cached MsObjModule* on cache hit. Returns NULL on error (runtime
   error already reported). Circular dependency -> returns MS_MOD_INITIALIZING module. */
MsObjModule* ms_module_load(MsVM* vm, const char* import_path,
                              const char* from_path);
