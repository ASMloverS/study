/* src/stdlib/log.c - STDLIB-07: log module
 * Provides: 6 log levels, configurable sink/format, tag sub-loggers.
 */
#include "ms/stdlib_register.h"
#include "ms/stdlib/objfile.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>

/* ================================================================
 * Global log state
 * ================================================================ */

static struct {
    int   level;        /* global minimum level */
    FILE* sink;         /* output target; NULL = stderr (lazy) */
    char  format[256];  /* format template */
    bool  fatal_exits;
} g_log = {
    .level       = 2,   /* INFO */
    .sink        = NULL,
    .format      = "{time} [{level}] {msg}",
    .fatal_exits = true,
};

/* ================================================================
 * Logger userdata (sub-logger)
 * ================================================================ */

typedef struct {
    char* tag;   /* strdup; freed in finalize */
    int   level; /* -1 = inherit global */
} MsLoggerData;

static void logger_finalize(void* data) {
    MsLoggerData* ld = (MsLoggerData*)data;
    if (ld->tag) { free(ld->tag); ld->tag = NULL; }
}

static bool is_logger(MsValue v) {
    return MS_IS_USERDATA(v) &&
           strcmp(MS_AS_USERDATA(v)->type_tag, "MsLogger") == 0;
}

/* ================================================================
 * Level helpers
 * ================================================================ */

static const char* const kLevelNames[6] = {
    "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
};

static int parse_level(MsVM* vm, MsValue v, const char* fn_name) {
    if (MS_IS_INT(v)) {
        int l = (int)MS_AS_INT(v);
        if (l < 0 || l > 5) {
            ms_vm_runtime_error(vm, "%s: level must be 0..5", fn_name);
            return -2;
        }
        return l;
    }
    if (MS_IS_STRING(v)) {
        const char* s = MS_AS_CSTRING(v);
        for (int i = 0; i < 6; i++) {
            /* compare case-insensitively against trimmed level names */
            char tmp[8];
            memcpy(tmp, kLevelNames[i], 7);
            tmp[7] = '\0';
            /* strip trailing space */
            int n = (int)strlen(tmp);
            while (n > 0 && tmp[n-1] == ' ') tmp[--n] = '\0';
            if (strcmp(s, tmp) == 0) return i;
        }
        /* also accept lower-case */
        const char* names_lc[6] = {"trace","debug","info","warn","error","fatal"};
        for (int i = 0; i < 6; i++) {
            if (strcmp(s, names_lc[i]) == 0) return i;
        }
        ms_vm_runtime_error(vm, "%s: unknown level '%s'", fn_name, s);
        return -2;
    }
    ms_vm_runtime_error(vm, "%s: level must be string or int", fn_name);
    return -2;
}

/* ================================================================
 * Format helpers
 * ================================================================ */

/* Format message body: replace %s %d %f %v with argv[0..argc-1] */
static void format_msg(MsObjStringBuilder* sb,
                       const char* fmt, int argc, MsValue* argv) {
    int arg_idx = 0;
    const char* p = fmt;
    char tmp[64];
    while (*p) {
        if (*p == '%' && *(p+1) != '\0') {
            char spec = *(p+1);
            if ((spec == 's' || spec == 'd' || spec == 'f' || spec == 'v')
                    && arg_idx < argc) {
                MsValue a = argv[arg_idx++];
                if (spec == 's' && MS_IS_STRING(a)) {
                    MsObjString* s = MS_AS_STRING(a);
                    ms_obj_sb_append(sb, s->data, s->length);
                } else if (spec == 'd') {
                    ms_i64 iv = MS_IS_INT(a) ? MS_AS_INT(a) :
                                MS_IS_NUMBER(a) ? (ms_i64)MS_AS_NUMBER(a) : 0;
                    int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)iv);
                    ms_obj_sb_append(sb, tmp, n);
                } else if (spec == 'f') {
                    double dv = MS_IS_NUMBER(a) ? MS_AS_NUMBER(a) :
                                MS_IS_INT(a)    ? (double)MS_AS_INT(a) : 0.0;
                    int n = snprintf(tmp, sizeof(tmp), "%g", dv);
                    ms_obj_sb_append(sb, tmp, n);
                } else {
                    /* %v or %s on non-string: use ms_value_to_cstring */
                    char* s = ms_value_to_cstring(a);
                    ms_obj_sb_append(sb, s, (int)strlen(s));
                    free(s);
                }
                p += 2;
                continue;
            }
        }
        ms_obj_sb_append(sb, p, 1);
        p++;
    }
}

/* Build ISO-8601 local time string like "2026-05-23T14:30:00" */
static void time_str(char* out, size_t cap) {
    time_t t = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &t) != 0) {
        if (cap > 0) out[0] = '\0';
        return;
    }
#else
    if (!localtime_r(&t, &tm_buf)) {
        if (cap > 0) out[0] = '\0';
        return;
    }
#endif
    strftime(out, cap, "%Y-%m-%dT%H:%M:%S", &tm_buf);
}

/* Substitute {time} {level} {tag} {msg} in g_log.format */
static void format_line(MsObjStringBuilder* sb,
                        int level, const char* tag, const char* msg) {
    char ts[32];
    time_str(ts, sizeof(ts));
    const char* lname = kLevelNames[level]; /* 5 chars, may have trailing space */
    const char* p = g_log.format;
    while (*p) {
        if (*p == '{') {
            if (strncmp(p, "{time}",  6) == 0) {
                ms_obj_sb_append(sb, ts, (int)strlen(ts));
                p += 6; continue;
            }
            if (strncmp(p, "{level}", 7) == 0) {
                ms_obj_sb_append(sb, lname, (int)strlen(lname));
                p += 7; continue;
            }
            if (strncmp(p, "{tag}",  5) == 0) {
                if (tag && *tag) ms_obj_sb_append(sb, tag, (int)strlen(tag));
                p += 5; continue;
            }
            if (strncmp(p, "{msg}",  5) == 0) {
                ms_obj_sb_append(sb, msg, (int)strlen(msg));
                p += 5; continue;
            }
        }
        ms_obj_sb_append(sb, p, 1);
        p++;
    }
}

/* ================================================================
 * Core emit
 * ================================================================ */

static void emit(MsVM* vm, int level, const char* tag,
                 const char* msg_fmt, int argc, MsValue* argv) {
    /* Build message body */
    MsObjStringBuilder* msb = ms_obj_sb_new(vm);
    format_msg(msb, msg_fmt, argc, argv);
    MsObjString* msg_obj = ms_obj_sb_to_string(vm, msb);

    /* Build full line */
    MsObjStringBuilder* lsb = ms_obj_sb_new(vm);
    format_line(lsb, level, tag, msg_obj->data);
    MsObjString* line_obj = ms_obj_sb_to_string(vm, lsb);

    FILE* out = g_log.sink ? g_log.sink : stderr;
    fprintf(out, "%s\n", line_obj->data);

    if (level == 5 && g_log.fatal_exits) exit(1);
}

/* ================================================================
 * Global logger natives
 * ================================================================ */

static MsValue log_at(MsVM* vm, int level, int argc, MsValue* argv) {
    if (argc < 1) {
        ms_vm_runtime_error(vm, "log: need at least one argument");
        return MS_NIL_VAL();
    }
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "log: first argument must be a string");
        return MS_NIL_VAL();
    }
    if (level < g_log.level) return MS_NIL_VAL();
    const char* fmt = MS_AS_CSTRING(argv[0]);
    emit(vm, level, "", fmt, argc - 1, argv + 1);
    return MS_NIL_VAL();
}

static MsValue ms_log_trace(MsVM* vm, int argc, MsValue* argv) { return log_at(vm,0,argc,argv); }
static MsValue ms_log_debug(MsVM* vm, int argc, MsValue* argv) { return log_at(vm,1,argc,argv); }
static MsValue ms_log_info (MsVM* vm, int argc, MsValue* argv) { return log_at(vm,2,argc,argv); }
static MsValue ms_log_warn (MsVM* vm, int argc, MsValue* argv) { return log_at(vm,3,argc,argv); }
static MsValue ms_log_error(MsVM* vm, int argc, MsValue* argv) { return log_at(vm,4,argc,argv); }
static MsValue ms_log_fatal(MsVM* vm, int argc, MsValue* argv) { return log_at(vm,5,argc,argv); }

static MsValue ms_log_set_level(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    int l = parse_level(vm, argv[0], "log.set_level");
    if (l >= 0) g_log.level = l;
    return MS_NIL_VAL();
}

static MsValue ms_log_get_level(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc; (void)argv;
    /* Return trimmed level name */
    const char* raw = kLevelNames[g_log.level];
    char buf[8];
    memcpy(buf, raw, 7); buf[7] = '\0';
    int n = (int)strlen(buf);
    while (n > 0 && buf[n-1] == ' ') buf[--n] = '\0';
    return MS_OBJ_VAL(ms_obj_string_copy(vm, buf, n));
}

static MsValue ms_log_set_sink(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (MS_IS_NIL(argv[0])) {
        g_log.sink = NULL;
        return MS_NIL_VAL();
    }
    if (MS_IS_FILE(argv[0])) {
        MsObjFile* f = MS_AS_FILE(argv[0]);
        if (!f->fp) {
            ms_vm_runtime_error(vm, "log.set_sink: file is closed");
            return MS_NIL_VAL();
        }
        g_log.sink = f->fp;
        return MS_NIL_VAL();
    }
    ms_vm_runtime_error(vm, "log.set_sink: expected File or nil");
    return MS_NIL_VAL();
}

static MsValue ms_log_set_format(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "log.set_format: expected string");
        return MS_NIL_VAL();
    }
    const char* s = MS_AS_CSTRING(argv[0]);
    size_t slen = strlen(s);
    size_t copy_n = slen < sizeof(g_log.format) - 1 ? slen : sizeof(g_log.format) - 1;
    memcpy(g_log.format, s, copy_n);
    g_log.format[copy_n] = '\0';
    return MS_NIL_VAL();
}

static MsValue ms_log_set_fatal_exits(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BOOL(argv[0])) {
        ms_vm_runtime_error(vm, "log.set_fatal_exits: expected bool");
        return MS_NIL_VAL();
    }
    g_log.fatal_exits = MS_AS_BOOL(argv[0]);
    return MS_NIL_VAL();
}

/* log.with(tag) → Logger userdata */
static MsValue ms_log_with(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "log.with: expected string tag");
        return MS_NIL_VAL();
    }
    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(MsLoggerData),
                                            logger_finalize, NULL, "MsLogger");
    MsLoggerData* ld = (MsLoggerData*)ud->data;
    ld->tag   = ms_strdup(MS_AS_CSTRING(argv[0]));
    ld->level = -1; /* inherit global */
    return MS_OBJ_VAL(ud);
}

/* ================================================================
 * Sub-logger method dispatch (called from ms_builtin_invoke)
 * ================================================================ */

static MsValue logger_log_at(MsVM* vm, MsLoggerData* ld, int level,
                              int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "Logger: need string message");
        return MS_NIL_VAL();
    }
    int eff_level = (ld->level >= 0) ? ld->level : g_log.level;
    if (level < eff_level) return MS_NIL_VAL();
    const char* fmt = MS_AS_CSTRING(argv[0]);
    emit(vm, level, ld->tag ? ld->tag : "", fmt, argc - 1, argv + 1);
    return MS_NIL_VAL();
}

bool ms_logger_invoke(MsVM* vm, MsObjUserdata* ud,
                      MsObjString* method, int argc, MsValue* argv,
                      MsValue* out) {
    if (strcmp(ud->type_tag, "MsLogger") != 0) return false;
    MsLoggerData* ld = (MsLoggerData*)ud->data;
    const char* m = method->data;

    if (strcmp(m, "trace") == 0) { *out = logger_log_at(vm,ld,0,argc,argv); return true; }
    if (strcmp(m, "debug") == 0) { *out = logger_log_at(vm,ld,1,argc,argv); return true; }
    if (strcmp(m, "info")  == 0) { *out = logger_log_at(vm,ld,2,argc,argv); return true; }
    if (strcmp(m, "warn")  == 0) { *out = logger_log_at(vm,ld,3,argc,argv); return true; }
    if (strcmp(m, "error") == 0) { *out = logger_log_at(vm,ld,4,argc,argv); return true; }
    if (strcmp(m, "fatal") == 0) { *out = logger_log_at(vm,ld,5,argc,argv); return true; }
    if (strcmp(m, "set_level") == 0) {
        if (argc < 1) { ms_vm_runtime_error(vm, "Logger.set_level: need level"); *out = MS_NIL_VAL(); return true; }
        int l = parse_level(vm, argv[0], "Logger.set_level");
        if (l >= 0) ld->level = l;
        *out = MS_NIL_VAL();
        return true;
    }
    return false;
}

/* ================================================================
 * Registration
 * ================================================================ */

static const MsNativeDef ms_log_defs[] = {
    {"trace",           ms_log_trace,          -1},
    {"debug",           ms_log_debug,          -1},
    {"info",            ms_log_info,           -1},
    {"warn",            ms_log_warn,           -1},
    {"error",           ms_log_error,          -1},
    {"fatal",           ms_log_fatal,          -1},
    {"set_level",       ms_log_set_level,       1},
    {"get_level",       ms_log_get_level,       0},
    {"set_sink",        ms_log_set_sink,        1},
    {"set_format",      ms_log_set_format,      1},
    {"set_fatal_exits", ms_log_set_fatal_exits, 1},
    {"with",            ms_log_with,            1},
    {NULL, NULL, 0}
};

void ms_module_log_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_log_defs);
}
