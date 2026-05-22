/* STDLIB-02: os module — env/fs/proc interface */
#include "ms/stdlib_register.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/vtable.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <direct.h>   /* _getcwd, _chdir, _mkdir */
#  include <io.h>       /* _unlink */
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  include <dirent.h>
#  include <limits.h>   /* PATH_MAX */
#  include <errno.h>
#endif

/* ---- helpers ---- */

static MsValue make_str(MsVM* vm, const char* s) {
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s, (int)strlen(s)));
}

static const char* get_cstring(MsValue v) {
    return MS_AS_CSTRING(v);
}

/* Push a C string onto a List object. */
static void list_push_cstr(MsVM* vm, MsValue list, const char* s) {
    MsValue sv = make_str(vm, s);
    ms_value_array_push(&MS_AS_LIST(list)->items, sv);
}

/* Set a string key/value in a Map object. */
static void map_set_str(MsVM* vm, MsValue map, const char* key, MsValue val) {
    MsValue k = make_str(vm, key);
    ms_vtable_set(&MS_AS_MAP(map)->table, k, val);
}

/* ---- process ---- */

static MsValue ms_os_name(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
#if defined(_WIN32)
    return make_str(vm, "windows");
#elif defined(__linux__)
    return make_str(vm, "linux");
#elif defined(__APPLE__)
    return make_str(vm, "darwin");
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return make_str(vm, "bsd");
#else
    return make_str(vm, "unknown");
#endif
}

static MsValue ms_os_pid(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc; (void)argv;
#ifdef _WIN32
    return MS_INT_VAL((ms_i64)GetCurrentProcessId());
#else
    return MS_INT_VAL((ms_i64)getpid());
#endif
}

static MsValue ms_os_argv(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    MsValue list = MS_OBJ_VAL(ms_obj_list_new(vm));
    for (int i = 0; i < vm->main_argc; i++) {
        list_push_cstr(vm, list, vm->main_argv[i]);
    }
    return list;
}

static MsValue ms_os_exit(MsVM* vm, int argc, MsValue* argv) {
    (void)vm;
    int code = (argc >= 1 && MS_IS_INT(argv[0])) ? (int)MS_AS_INT(argv[0]) : 0;
    exit(code);
}

/* ---- env ---- */

static MsValue ms_os_env(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* key = get_cstring(argv[0]);
#ifdef _WIN32
    char* val = NULL; size_t len = 0;
    if (_dupenv_s(&val, &len, key) != 0 || val == NULL) return MS_NIL_VAL();
    MsValue result = make_str(vm, val);
    free(val);
    return result;
#else
    const char* val = getenv(key);
    if (!val) return MS_NIL_VAL();
    return make_str(vm, val);
#endif
}

static MsValue ms_os_setenv(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* key = get_cstring(argv[0]);
    const char* val = get_cstring(argv[1]);
#ifdef _WIN32
    char buf[32768];
    snprintf(buf, sizeof(buf), "%s=%s", key, val);
    if (_putenv(buf) != 0) {
        ms_vm_runtime_error(vm, "os.setenv: failed to set '%s'", key);
    }
#else
    if (setenv(key, val, 1) != 0) {
        ms_vm_runtime_error(vm, "os.setenv: %s", strerror(errno));
    }
#endif
    return MS_NIL_VAL();
}

static MsValue ms_os_unsetenv(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    const char* key = get_cstring(argv[0]);
#ifdef _WIN32
    char buf[32768];
    snprintf(buf, sizeof(buf), "%s=", key);
    _putenv(buf);
#else
    unsetenv(key);
#endif
    return MS_NIL_VAL();
}

static MsValue ms_os_environ(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    MsValue map = MS_OBJ_VAL(ms_obj_map_new(vm));
#ifdef _WIN32
    LPCH env = GetEnvironmentStringsA();
    if (env) {
        LPCH p = env;
        while (*p) {
            /* Each entry is "KEY=VALUE\0"; skip entries starting with '=' */
            const char* eq = strchr(p, '=');
            if (eq && eq != p) {
                int klen = (int)(eq - p);
                MsObjString* ks = ms_obj_string_copy(vm, p, klen);
                MsValue k = MS_OBJ_VAL(ks);
                MsValue v = make_str(vm, eq + 1);
                ms_vtable_set(&MS_AS_MAP(map)->table, k, v);
            }
            p += strlen(p) + 1;
        }
        FreeEnvironmentStringsA(env);
    }
#else
    extern char** environ;
    for (char** ep = environ; ep && *ep; ep++) {
        const char* eq = strchr(*ep, '=');
        if (eq && eq != *ep) {
            int klen = (int)(eq - *ep);
            MsObjString* ks = ms_obj_string_copy(vm, *ep, klen);
            MsValue k = MS_OBJ_VAL(ks);
            MsValue v = make_str(vm, eq + 1);
            ms_vtable_set(&MS_AS_MAP(map)->table, k, v);
        }
    }
#endif
    return map;
}

/* ---- filesystem metadata ---- */

static MsValue ms_os_cwd(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
#ifdef _WIN32
    char buf[MAX_PATH];
    if (!GetCurrentDirectoryA(MAX_PATH, buf)) {
        ms_vm_runtime_error(vm, "os.cwd: failed");
        return MS_NIL_VAL();
    }
    return make_str(vm, buf);
#else
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) {
        ms_vm_runtime_error(vm, "os.cwd: %s", strerror(errno));
        return MS_NIL_VAL();
    }
    return make_str(vm, buf);
#endif
}

static MsValue ms_os_chdir(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
#ifdef _WIN32
    if (!SetCurrentDirectoryA(path)) {
        ms_vm_runtime_error(vm, "os.chdir: failed for '%s'", path);
    }
#else
    if (chdir(path) != 0) {
        ms_vm_runtime_error(vm, "os.chdir: %s", strerror(errno));
    }
#endif
    return MS_NIL_VAL();
}

static MsValue ms_os_exists(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    const char* path = get_cstring(argv[0]);
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return MS_BOOL_VAL(attr != INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    return MS_BOOL_VAL(stat(path, &st) == 0);
#endif
}

static MsValue ms_os_isfile(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    const char* path = get_cstring(argv[0]);
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(!(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) != 0) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(S_ISREG(st.st_mode));
#endif
}

static MsValue ms_os_isdir(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    const char* path = get_cstring(argv[0]);
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(!!(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) != 0) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(S_ISDIR(st.st_mode));
#endif
}

static MsValue ms_os_mkdir(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
#ifdef _WIN32
    if (!CreateDirectoryA(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ms_vm_runtime_error(vm, "os.mkdir: failed for '%s'", path);
    }
#else
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        ms_vm_runtime_error(vm, "os.mkdir: %s", strerror(errno));
    }
#endif
    return MS_NIL_VAL();
}

/* Recursive mkdir -p */
static bool makedirs_recursive(const char* path) {
    size_t len = strlen(path);
    if (len == 0) return true;
    char* buf = (char*)malloc(len + 1);
    if (!buf) return false;
    memcpy(buf, path, len + 1);

    /* Walk path, creating each component */
    for (size_t i = 1; i <= len; i++) {
        bool is_sep = (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0');
        if (is_sep) {
            char saved = buf[i];
            buf[i] = '\0';
#ifdef _WIN32
            if (!CreateDirectoryA(buf, NULL)) {
                DWORD err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS && err != ERROR_ACCESS_DENIED) {
                    free(buf); return false;
                }
            }
#else
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                free(buf); return false;
            }
#endif
            buf[i] = saved;
        }
    }
    free(buf);
    return true;
}

static MsValue ms_os_makedirs(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
    if (!makedirs_recursive(path)) {
        ms_vm_runtime_error(vm, "os.makedirs: failed for '%s'", path);
    }
    return MS_NIL_VAL();
}

static MsValue ms_os_rmdir(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
#ifdef _WIN32
    if (!RemoveDirectoryA(path)) {
        ms_vm_runtime_error(vm, "os.rmdir: failed for '%s'", path);
    }
#else
    if (rmdir(path) != 0) {
        ms_vm_runtime_error(vm, "os.rmdir: %s", strerror(errno));
    }
#endif
    return MS_NIL_VAL();
}

static MsValue ms_os_remove(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
#ifdef _WIN32
    if (!DeleteFileA(path)) {
        ms_vm_runtime_error(vm, "os.remove: failed for '%s'", path);
    }
#else
    if (unlink(path) != 0) {
        ms_vm_runtime_error(vm, "os.remove: %s", strerror(errno));
    }
#endif
    return MS_NIL_VAL();
}

static MsValue ms_os_rename(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* src = get_cstring(argv[0]);
    const char* dst = get_cstring(argv[1]);
#ifdef _WIN32
    if (!MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING)) {
        ms_vm_runtime_error(vm, "os.rename: failed '%s' -> '%s'", src, dst);
    }
#else
    if (rename(src, dst) != 0) {
        ms_vm_runtime_error(vm, "os.rename: %s", strerror(errno));
    }
#endif
    return MS_NIL_VAL();
}

static MsValue ms_os_listdir(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
    MsValue list = MS_OBJ_VAL(ms_obj_list_new(vm));
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s\\*", path);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return list; /* return empty list on error */
    }
    do {
        if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
            list_push_cstr(vm, list, fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(path);
    if (!d) {
        ms_vm_runtime_error(vm, "os.listdir: %s", strerror(errno));
        return list;
    }
    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        list_push_cstr(vm, list, ent->d_name);
    }
    closedir(d);
#endif
    return list;
}

static MsValue ms_os_stat(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
    MsValue map = MS_OBJ_VAL(ms_obj_map_new(vm));

#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) {
        ms_vm_runtime_error(vm, "os.stat: failed for '%s'", path);
        return map;
    }
    ULARGE_INTEGER sz;
    sz.LowPart  = d.nFileSizeLow;
    sz.HighPart = d.nFileSizeHigh;
    ULARGE_INTEGER mt;
    mt.LowPart  = d.ftLastWriteTime.dwLowDateTime;
    mt.HighPart = d.ftLastWriteTime.dwHighDateTime;
    ULARGE_INTEGER ct;
    ct.LowPart  = d.ftCreationTime.dwLowDateTime;
    ct.HighPart = d.ftCreationTime.dwHighDateTime;
    /* FILETIME 100-ns ticks since 1601-01-01; convert to seconds */
    int64_t mtime_s = (int64_t)((mt.QuadPart - 116444736000000000ULL) / 10000000ULL);
    int64_t ctime_s = (int64_t)((ct.QuadPart - 116444736000000000ULL) / 10000000ULL);
    bool is_dir  = !!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    bool is_file = !is_dir;
    map_set_str(vm, map, "size",   MS_INT_VAL((ms_i64)sz.QuadPart));
    map_set_str(vm, map, "mtime",  MS_INT_VAL((ms_i64)mtime_s));
    map_set_str(vm, map, "ctime",  MS_INT_VAL((ms_i64)ctime_s));
    map_set_str(vm, map, "isdir",  MS_BOOL_VAL(is_dir));
    map_set_str(vm, map, "isfile", MS_BOOL_VAL(is_file));
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        ms_vm_runtime_error(vm, "os.stat: %s", strerror(errno));
        return map;
    }
    map_set_str(vm, map, "size",   MS_INT_VAL((ms_i64)st.st_size));
    map_set_str(vm, map, "mtime",  MS_INT_VAL((ms_i64)st.st_mtime));
    map_set_str(vm, map, "ctime",  MS_INT_VAL((ms_i64)st.st_ctime));
    map_set_str(vm, map, "isdir",  MS_BOOL_VAL(S_ISDIR(st.st_mode)));
    map_set_str(vm, map, "isfile", MS_BOOL_VAL(S_ISREG(st.st_mode)));
#endif
    return map;
}

static MsValue ms_os_realpath(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetFullPathNameA(path, MAX_PATH, buf, NULL);
    if (n == 0 || n >= MAX_PATH) {
        ms_vm_runtime_error(vm, "os.realpath: failed for '%s'", path);
        return MS_NIL_VAL();
    }
    return make_str(vm, buf);
#else
    char buf[PATH_MAX];
    if (!realpath(path, buf)) {
        ms_vm_runtime_error(vm, "os.realpath: %s", strerror(errno));
        return MS_NIL_VAL();
    }
    return make_str(vm, buf);
#endif
}

/* ---- path manipulation ---- */

static MsValue ms_os_join(MsVM* vm, int argc, MsValue* argv) {
    if (argc == 0) return make_str(vm, "");
    /* Concatenate parts with platform separator, last absolute component wins */
#ifdef _WIN32
    const char SEP = '\\';
#else
    const char SEP = '/';
#endif
    /* Build into a growable buffer */
    size_t cap = 256;
    char* buf = (char*)malloc(cap);
    if (!buf) { ms_vm_runtime_error(vm, "os.join: OOM"); return MS_NIL_VAL(); }
    size_t pos = 0;

    for (int i = 0; i < argc; i++) {
        const char* part = get_cstring(argv[i]);
        size_t plen = strlen(part);
        /* If this part is absolute, reset the buffer */
        bool is_abs = (plen > 0 && (part[0] == '/' || part[0] == '\\'));
#ifdef _WIN32
        if (!is_abs && plen >= 2 && part[1] == ':') is_abs = true;
#endif
        if (is_abs) pos = 0;
        /* Add separator if needed */
        if (pos > 0 && buf[pos - 1] != '/' && buf[pos - 1] != '\\') {
            if (pos + 1 >= cap) {
                cap *= 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) { free(buf); ms_vm_runtime_error(vm, "os.join: OOM"); return MS_NIL_VAL(); }
                buf = nb;
            }
            buf[pos++] = SEP;
        }
        /* Append part */
        while (pos + plen + 1 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); ms_vm_runtime_error(vm, "os.join: OOM"); return MS_NIL_VAL(); }
            buf = nb;
        }
        memcpy(buf + pos, part, plen);
        pos += plen;
    }
    buf[pos] = '\0';
    MsValue result = make_str(vm, buf);
    free(buf);
    return result;
}

static MsValue ms_os_basename(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
    const char* last = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return make_str(vm, last);
}

static MsValue ms_os_dirname(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
    int len = (int)strlen(path);
    /* Strip trailing separators */
    int end = len - 1;
    while (end > 0 && (path[end] == '/' || path[end] == '\\')) end--;
    /* Find last separator */
    int last_sep = -1;
    for (int i = end; i >= 0; i--) {
        if (path[i] == '/' || path[i] == '\\') { last_sep = i; break; }
    }
    if (last_sep < 0) return make_str(vm, ".");
    if (last_sep == 0) return make_str(vm, "/");
    MsObjString* s = ms_obj_string_copy(vm, path, last_sep);
    return MS_OBJ_VAL(s);
}

static MsValue ms_os_splitext(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = get_cstring(argv[0]);
    /* Find last '.' after last separator */
    const char* last_dot = NULL;
    const char* p = path;
    for (; *p; p++) {
        if (*p == '/' || *p == '\\') last_dot = NULL;
        else if (*p == '.') last_dot = p;
    }
    MsValue stem, ext;
    if (!last_dot || last_dot == path) {
        stem = make_str(vm, path);
        ext  = make_str(vm, "");
    } else {
        stem = MS_OBJ_VAL(ms_obj_string_copy(vm, path, (int)(last_dot - path)));
        ext  = make_str(vm, last_dot);
    }
    MsValue items[2] = { stem, ext };
    return MS_OBJ_VAL(ms_obj_tuple_new(vm, items, 2));
}

/* ---- subprocess ---- */

static MsValue ms_os_exec(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    const char* cmd = get_cstring(argv[0]);
    int ret = system(cmd);
    return MS_INT_VAL((ms_i64)ret);
}

/* ---- registration ---- */

static const MsNativeDef ms_os_defs[] = {
    /* process */
    {"name",      ms_os_name,     0},
    {"pid",       ms_os_pid,      0},
    {"argv",      ms_os_argv,     0},
    {"exit",      ms_os_exit,    -1},
    /* env */
    {"env",       ms_os_env,      1},
    {"setenv",    ms_os_setenv,   2},
    {"unsetenv",  ms_os_unsetenv, 1},
    {"environ",   ms_os_environ,  0},
    /* fs metadata */
    {"cwd",       ms_os_cwd,      0},
    {"chdir",     ms_os_chdir,    1},
    {"exists",    ms_os_exists,   1},
    {"isfile",    ms_os_isfile,   1},
    {"isdir",     ms_os_isdir,    1},
    {"mkdir",     ms_os_mkdir,    1},
    {"makedirs",  ms_os_makedirs, 1},
    {"rmdir",     ms_os_rmdir,    1},
    {"remove",    ms_os_remove,   1},
    {"rename",    ms_os_rename,   2},
    {"listdir",   ms_os_listdir,  1},
    {"stat",      ms_os_stat,     1},
    {"realpath",  ms_os_realpath, 1},
    /* path */
    {"join",      ms_os_join,    -1},
    {"basename",  ms_os_basename, 1},
    {"dirname",   ms_os_dirname,  1},
    {"splitext",  ms_os_splitext, 1},
    /* subprocess */
    {"exec",      ms_os_exec,     1},
    {NULL, NULL, 0}
};

void ms_module_os_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_os_defs);

#ifdef _WIN32
    ms_module_export_value(vm, mod, "sep",     make_str(vm, "\\"));
    ms_module_export_value(vm, mod, "linesep", make_str(vm, "\r\n"));
    ms_module_export_value(vm, mod, "pathsep", make_str(vm, ";"));
    ms_module_export_value(vm, mod, "devnull", make_str(vm, "NUL"));
#else
    ms_module_export_value(vm, mod, "sep",     make_str(vm, "/"));
    ms_module_export_value(vm, mod, "linesep", make_str(vm, "\n"));
    ms_module_export_value(vm, mod, "pathsep", make_str(vm, ":"));
    ms_module_export_value(vm, mod, "devnull", make_str(vm, "/dev/null"));
#endif
}
