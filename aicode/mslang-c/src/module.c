#include "ms/module.h"
#include "ms/serializer.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

MsObjModule* ms_module_load(MsVM* vm, const char* import_path,
                              const char* from_path) {
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

    char* resolved = ms_resolve_path(import_path, from_dir);
    free(from_dir);
    if (!resolved) return NULL;

    /* Derive module name from import_path (last component, no extension) */
    int de = dir_end(import_path);
    const char* name_start = import_path + de + 1;
    int name_len = (int)strlen(name_start);
    /* Strip ".ms" suffix if present */
    if (name_len > 3 &&
        name_start[name_len - 3] == '.' &&
        name_start[name_len - 2] == 'm' &&
        name_start[name_len - 1] == 's') {
        name_len -= 3;
    }

    /* Cache lookup by resolved path */
    MsObjString* key = ms_obj_string_copy(vm, resolved, (int)strlen(resolved));
    MsValue cached;
    if (ms_table_get(&vm->module_cache, key, &cached)) {
        free(resolved);
        return MS_AS_MODULE(cached);
    }

    /* Create module, mark as initializing to detect circular deps */
    MsObjString* mod_name = ms_obj_string_copy(vm, name_start, name_len);
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
