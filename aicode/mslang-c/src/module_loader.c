#include "ms/module.h"
#include "ms/fs_util.h"
#include "ms/common.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <stdlib.h> /* _dupenv_s */
#endif

/* ---- internal growth helper ---- */

static bool ensure_cap(MsVM* vm) {
    if (vm->module_search_count < vm->module_search_cap) return true;
    int new_cap = vm->module_search_cap < 8 ? 8 : vm->module_search_cap * 2;
    char** arr = (char**)realloc(vm->module_search_paths,
                                 (size_t)new_cap * sizeof(char*));
    if (!arr) return false;
    vm->module_search_paths = arr;
    vm->module_search_cap   = new_cap;
    return true;
}

/* ---- public API ---- */

void ms_vm_add_search_path(MsVM* vm, const char* path) {
    if (!path || !*path) return;
    if (!ensure_cap(vm)) return;
    vm->module_search_paths[vm->module_search_count++] = ms_strdup(path);
}

void ms_vm_prepend_search_path(MsVM* vm, const char* path) {
    if (!path || !*path) return;
    if (!ensure_cap(vm)) return;
    /* Shift existing entries right by one */
    memmove(vm->module_search_paths + 1,
            vm->module_search_paths,
            (size_t)vm->module_search_count * sizeof(char*));
    vm->module_search_paths[0] = ms_strdup(path);
    vm->module_search_count++;
}

/* ---- MSLANG_PATH initialisation ---- */

void ms_load_mslang_path(MsVM* vm) {
#ifdef _WIN32
    char*  env     = NULL;
    size_t env_len = 0;
    if (_dupenv_s(&env, &env_len, "MSLANG_PATH") != 0 || !env) return;
    char delim = ';';
#else
    const char* env_raw = getenv("MSLANG_PATH");
    if (!env_raw) return;
    char* env = ms_strdup(env_raw);
    if (!env) return;
    char delim = ':';
#endif

    char** parts = NULL;
    int count = ms_string_split(env, delim, &parts);
    for (int i = 0; i < count; i++) {
        if (parts[i] && *parts[i]) {
            ms_vm_add_search_path(vm, parts[i]);
        }
        free(parts[i]);
    }
    free(parts);
    free(env);
}
