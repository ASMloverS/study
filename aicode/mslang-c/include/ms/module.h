#pragma once
#include "ms/vm.h"
#include <stdint.h>
#include <stddef.h>

/* ---- NativeDef: table-style native registration (CAPI-02) ---- */

/* One row in a MsNativeDef table.  Terminate with {NULL, NULL, 0}. */
typedef struct {
    const char* name;   /* export name; NULL sentinel terminates the table */
    MsNativeFn  fn;     /* native function pointer */
    int         arity;  /* expected arg count; -1 = variadic */
} MsNativeDef;

/* Style B - low-level primitive: register one native into mod->exports. */
void ms_module_def_native(MsVM*        vm,
                          MsObjModule* mod,
                          const char*  name,
                          MsNativeFn   fn,
                          int          arity);

/* Style A - table-style convenience wrapper (iterates until name == NULL). */
void ms_module_register_natives(MsVM*              vm,
                                MsObjModule*       mod,
                                const MsNativeDef* defs);

/* Export an arbitrary MsValue (constant, ObjFile, etc.) under name. */
void ms_module_export_value(MsVM*        vm,
                            MsObjModule* mod,
                            const char*  name,
                            MsValue      value);

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

/* ---- Search path API (CAPI-03) ---- */

/* Append path to the end of vm->module_search_paths (used for MSLANG_PATH). */
void ms_vm_add_search_path(MsVM* vm, const char* path);

/* Insert path at the front of vm->module_search_paths (used for --module-path). */
void ms_vm_prepend_search_path(MsVM* vm, const char* path);

/* Parse MSLANG_PATH env var and append each directory to vm->module_search_paths.
   Called at end of ms_vm_init. Implemented in src/module_loader.c. */
void ms_load_mslang_path(MsVM* vm);

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

/* ---- MsModuleApi: stable function table for dynamic extensions (CAPI-05) ---- */

#define MS_MODULE_API_VERSION 1

typedef struct MsModuleApi {
    uint32_t version; /* = MS_MODULE_API_VERSION */

    /* registration */
    void (*def_native)(MsVM* vm, MsObjModule* mod,
                       const char* name, MsNativeFn fn, int arity);
    void (*register_natives)(MsVM* vm, MsObjModule* mod,
                             const MsNativeDef* defs);
    void (*export_value)(MsVM* vm, MsObjModule* mod,
                         const char* name, MsValue v);

    /* value constructors */
    MsValue (*make_nil)(void);
    MsValue (*make_bool)(bool b);
    MsValue (*make_int)(int64_t i);
    MsValue (*make_number)(double d);
    MsValue (*make_string)(MsVM* vm, const char* s, int len);
    MsValue (*make_list)(MsVM* vm);
    MsValue (*make_map)(MsVM* vm);
    void    (*list_push)(MsVM* vm, MsValue list, MsValue v);
    void    (*map_set)(MsVM* vm, MsValue map, MsValue key, MsValue val);

    /* error */
    MsValue (*raise)(MsVM* vm, const char* msg);

    /* userdata */
    MsValue     (*userdata_new)(MsVM* vm, size_t bytes,
                                void (*finalize)(void* data),
                                void (*mark)(MsVM* vm, void* data),
                                const char* type_tag);
    void*       (*userdata_data)(MsValue v);
    const char* (*userdata_tag)(MsValue v);
    bool        (*userdata_is)(MsValue v, const char* tag);

    /* value inspection */
    bool        (*is_nil)(MsValue v);
    bool        (*is_bool)(MsValue v);
    bool        (*is_int)(MsValue v);
    bool        (*is_number)(MsValue v);
    bool        (*is_string)(MsValue v);
    bool        (*val_to_bool)(MsValue v);
    int64_t     (*val_to_int)(MsValue v);
    double      (*val_to_number)(MsValue v);
    const char* (*val_to_cstring)(MsValue v);
    int         (*string_len)(MsValue v);

    /* container / reflection (read-only) */
    bool    (*is_list)(MsValue v);
    bool    (*is_map)(MsValue v);
    bool    (*is_tuple)(MsValue v);
    bool    (*is_function)(MsValue v);
    bool    (*is_userdata)(MsValue v, const char* tag);
    int     (*list_len)(MsValue v);
    MsValue (*list_get)(MsValue v, int idx);
    bool    (*map_get)(MsValue v, MsValue key, MsValue* out);
} MsModuleApi;

/* Returns a pointer to the singleton MsModuleApi instance. */
const MsModuleApi* ms_module_api_get(void);
