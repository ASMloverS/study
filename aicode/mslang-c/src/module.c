#include "ms/module.h"
#include "ms/serializer.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- CAPI-02: NativeDef registration ---- */

void ms_module_def_native(MsVM* vm, MsObjModule* mod,
                           const char* name, MsNativeFn fn, int arity) {
    MsObjString* key = ms_obj_string_copy(vm, name, (int)strlen(name));
    MsObjNative* nat = ms_obj_native_new(vm, fn, name, arity);
    ms_table_set(&mod->exports, key, MS_OBJ_VAL(nat));
}

void ms_module_register_natives(MsVM* vm, MsObjModule* mod,
                                 const MsNativeDef* defs) {
    for (const MsNativeDef* d = defs; d->name != NULL; d++) {
        ms_module_def_native(vm, mod, d->name, d->fn, d->arity);
    }
}

void ms_module_export_value(MsVM* vm, MsObjModule* mod,
                             const char* name, MsValue value) {
    MsObjString* key = ms_obj_string_copy(vm, name, (int)strlen(name));
    ms_table_set(&mod->exports, key, value);
}

/* ---- builtin registry ---- */

void ms_vm_register_builtin_module(MsVM* vm, const char* name,
                                    MsBuiltinModuleInit init) {
    if (vm->builtin_count == vm->builtin_cap) {
        int new_cap = vm->builtin_cap < 8 ? 8 : vm->builtin_cap * 2;
        MsBuiltinModuleEntry* new_reg = (MsBuiltinModuleEntry*)realloc(
            vm->builtin_registry,
            (size_t)new_cap * sizeof(MsBuiltinModuleEntry));
        if (!new_reg) return; /* OOM: drop registration rather than corrupt state */
        vm->builtin_registry = new_reg;
        vm->builtin_cap = new_cap;
    }
    vm->builtin_registry[vm->builtin_count].name = name;
    vm->builtin_registry[vm->builtin_count].init = init;
    vm->builtin_count++;
}

MsBuiltinModuleInit ms_vm_find_builtin_module(MsVM* vm, const char* name) {
    for (int i = 0; i < vm->builtin_count; i++) {
        if (strcmp(vm->builtin_registry[i].name, name) == 0)
            return vm->builtin_registry[i].init;
    }
    return NULL;
}

/* ---- path helpers ---- */

/* Return pointer to directory portion end (last '/' or '\\'). */
static int dir_end(const char* path) {
    int last = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/' || path[i] == '\\') last = i;
    }
    return last;
}

/* Return true iff path has a file extension. */
static bool has_extension(const char* path) {
    for (int i = (int)strlen(path) - 1; i >= 0; i--) {
        if (path[i] == '.') return true;
        if (path[i] == '/' || path[i] == '\\') break;
    }
    return false;
}

char* ms_resolve_path(const char* import_path, const char* from_dir) {
    /* Build base: from_dir + "/" + import_path */
    size_t dir_len = from_dir ? strlen(from_dir) : 0;
    size_t imp_len = strlen(import_path);
    /* +2 for "/" separator and NUL, +3 for ".ms" */
    size_t buf_size = dir_len + 1 + imp_len + 4;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return NULL;

    if (dir_len > 0) {
        memcpy(buf, from_dir, dir_len);
        buf[dir_len] = '/';
        memcpy(buf + dir_len + 1, import_path, imp_len);
        buf[dir_len + 1 + imp_len] = '\0';
    } else {
        memcpy(buf, import_path, imp_len);
        buf[imp_len] = '\0';
    }

    /* Normalize path separators to '/' */
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '\\') buf[i] = '/';
    }

    /* Append ".ms" if no extension */
    if (!has_extension(buf)) {
        size_t cur = strlen(buf);
        buf[cur]     = '.';
        buf[cur + 1] = 'm';
        buf[cur + 2] = 's';
        buf[cur + 3] = '\0';
    }

    return buf;
}

char* ms_read_file(const char* path) {
    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[read] = '\0';
    return buf;
}

/* ---- module load ---- */

/* Extract the last path component from import_path (no allocation).
   The caller is responsible for stripping the ".ms" suffix if needed. */
static const char* extract_module_name(const char* import_path) {
    int de = dir_end(import_path);
    return import_path + de + 1;
}

MsObjModule* ms_module_load(MsVM* vm, const char* import_path,
                              const char* from_path) {
    /* 1. Extract bare module name (strips directory + .ms) */
    const char* raw_name = extract_module_name(import_path);
    int raw_len = (int)strlen(raw_name);
    /* Strip ".ms" if present to get the pure name */
    char pure_name[256];
    int pure_len = raw_len;
    if (pure_len > 3 &&
        raw_name[pure_len - 3] == '.' &&
        raw_name[pure_len - 2] == 'm' &&
        raw_name[pure_len - 1] == 's') {
        pure_len -= 3;
    }
    if (pure_len >= (int)sizeof(pure_name)) pure_len = (int)sizeof(pure_name) - 1;
    memcpy(pure_name, raw_name, (size_t)pure_len);
    pure_name[pure_len] = '\0';

    /* 2. Builtin registry lookup (before any filesystem access) */
    MsBuiltinModuleInit builtin_init = ms_vm_find_builtin_module(vm, pure_name);
    if (builtin_init) {
        char synth_path[280];
        snprintf(synth_path, sizeof(synth_path), "<builtin:%s>", pure_name);

        MsObjString* key = ms_obj_string_copy(vm, synth_path,
                                               (int)strlen(synth_path));
        MsValue cached;
        if (ms_table_get(&vm->module_cache, key, &cached))
            return MS_AS_MODULE(cached);

        MsObjString* mod_name = ms_obj_string_copy(vm, pure_name, pure_len);
        MsObjModule* mod = ms_obj_module_new(vm, mod_name, key);
        mod->state = MS_MOD_INITIALIZING;
        ms_table_set(&vm->module_cache, key, MS_OBJ_VAL(mod));

        builtin_init(vm, mod);

        mod->state = MS_MOD_INITIALIZED;
        return mod;
    }

    /* 3. Filesystem path resolution with search path fallback */

    /* Derive from_dir from from_path */
    char* from_dir = NULL;
    if (from_path) {
        int de = dir_end(from_path);
        if (de >= 0) {
            from_dir = (char*)malloc((size_t)de + 1);
            if (!from_dir) return NULL;
            memcpy(from_dir, from_path, (size_t)de);
            from_dir[de] = '\0';
            /* Normalize */
            for (int i = 0; from_dir[i]; i++) {
                if (from_dir[i] == '\\') from_dir[i] = '/';
            }
        }
    }

    /* Try resolution in order:
       1. from_dir (script directory)
       2. each vm->module_search_paths entry
       3. current working directory (NULL from_dir) */
    char* resolved = NULL;
    {
        char* candidate = ms_resolve_path(import_path, from_dir);
        if (candidate) {
            FILE* test = NULL;
#ifdef _MSC_VER
            fopen_s(&test, candidate, "rb");
#else
            test = fopen(candidate, "rb");
#endif
            if (test) { fclose(test); resolved = candidate; }
            else free(candidate);
        }
    }
    if (!resolved) {
        for (int si = 0; si < vm->module_search_count && !resolved; si++) {
            char* candidate = ms_resolve_path(import_path,
                                               vm->module_search_paths[si]);
            if (candidate) {
                FILE* test = NULL;
#ifdef _MSC_VER
                fopen_s(&test, candidate, "rb");
#else
                test = fopen(candidate, "rb");
#endif
                if (test) { fclose(test); resolved = candidate; }
                else free(candidate);
            }
        }
    }
    if (!resolved) {
        /* CWD fallback */
        resolved = ms_resolve_path(import_path, NULL);
    }
    free(from_dir);
    if (!resolved) return NULL;

    /* Cache lookup by resolved path */
    MsObjString* key = ms_obj_string_copy(vm, resolved, (int)strlen(resolved));
    MsValue cached;
    if (ms_table_get(&vm->module_cache, key, &cached)) {
        free(resolved);
        return MS_AS_MODULE(cached);
    }

    /* Create module, mark as initializing to detect circular deps */
    MsObjString* mod_name = ms_obj_string_copy(vm, pure_name, pure_len);
    MsObjModule* mod = ms_obj_module_new(vm, mod_name, key);
    mod->state = MS_MOD_INITIALIZING;
    ms_table_set(&vm->module_cache, key, MS_OBJ_VAL(mod));

    /* Compile (cached: writes to __mscache__/<module>.msc on first load) */
    MsObjFunction* fn = ms_compile_cached(vm, resolved, 0);
    free(resolved);
    if (!fn) {
        mod->state = MS_MOD_FAILED;
        return NULL;
    }

    /* Execute module top-level code */
    MsInterpretResult r = ms_vm_execute_module(vm, fn, mod);
    if (r != MS_INTERPRET_OK) {
        mod->state = MS_MOD_FAILED;
        return NULL;
    }

    mod->state = MS_MOD_INITIALIZED;
    return mod;
}
