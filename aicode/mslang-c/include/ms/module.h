#pragma once
#include "ms/vm.h"

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
