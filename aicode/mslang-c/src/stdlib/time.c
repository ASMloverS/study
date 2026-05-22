#include "ms/module.h"
#include "ms/vm.h"
#include "ms/event_loop.h"
#include "ms/value.h"
#include "ms/object.h"
#include "ms/vtable.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#endif

static void ensure_loop(MsVM* vm) {
    if (!vm->loop_inited) {
        ms_loop_init(&vm->event_loop, vm);
        vm->loop_inited = true;
    }
}

/* ---- time.now() ---- */

#ifdef _WIN32
static MsValue native_time_now(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm); MS_UNUSED(argc); MS_UNUSED(argv);
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ul;
    ul.LowPart  = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    double secs = (double)(ul.QuadPart - 116444736000000000ULL) / 1e7;
    return MS_NUMBER_VAL(secs);
}
#else
static MsValue native_time_now(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm); MS_UNUSED(argc); MS_UNUSED(argv);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return MS_NUMBER_VAL((double)ts.tv_sec + ts.tv_nsec / 1e9);
}
#endif

/* ---- time.now_ms() ---- */

static MsValue native_time_now_ms(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(argc); MS_UNUSED(argv);
    MsValue v = native_time_now(vm, 0, NULL);
    return MS_INT_VAL((ms_i64)(MS_AS_NUMBER(v) * 1000.0));
}

/* ---- time.monotonic_ms() ---- */

static MsValue native_time_monotonic_ms(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm); MS_UNUSED(argc); MS_UNUSED(argv);
    return MS_INT_VAL((ms_i64)ms_monotonic_ms());
}

/* ---- time.clock() ---- */

static MsValue native_time_clock(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm); MS_UNUSED(argc); MS_UNUSED(argv);
    return MS_NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

/* ---- time.sleep(ms) -> Future ---- */

static MsValue native_time_sleep(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || (!MS_IS_INT(argv[0]) && !MS_IS_NUMBER(argv[0]))) {
        ms_vm_runtime_error(vm, "time.sleep() requires a numeric argument.");
        return MS_NIL_VAL();
    }
    uint64_t delay = MS_IS_INT(argv[0])
                     ? (uint64_t)MS_AS_INT(argv[0])
                     : (uint64_t)MS_AS_NUMBER(argv[0]);
    ensure_loop(vm);
    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_loop_call_later(&vm->event_loop, delay, fut);
    return MS_OBJ_VAL((MsObject*)fut);
}

/* ---- time.run_until_complete(future) ---- */

static MsValue native_time_run_until_complete(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_FUTURE(argv[0])) {
        ms_vm_runtime_error(vm, "time.run_until_complete() requires a Future argument.");
        return MS_NIL_VAL();
    }
    ensure_loop(vm);
    MsObjFuture* fut = MS_AS_FUTURE(argv[0]);
    int r = ms_loop_run_until_complete(&vm->event_loop, fut);
    if (r != 0) return MS_NIL_VAL();
    return fut->result;
}

/* ---- gather() helpers & time.gather(list) ---- */

typedef struct MsGatherCtx {
    MsObjFuture* parent;
    MsValue*     results;
    int          total;
    int          remaining;
} MsGatherCtx;

static void gather_on_resolve(MsVM* vm, void* userdata, int index, MsValue result) {
    MsGatherCtx* ctx = (MsGatherCtx*)userdata;
    if (ctx->parent->state != MS_FUTURE_PENDING) return;
    ctx->results[index] = result;
    if (--ctx->remaining == 0) {
        MsObjList* lst = ms_obj_list_from_array(vm, ctx->results, ctx->total);
        ms_future_resolve(vm, ctx->parent, MS_OBJ_VAL((MsObject*)lst));
        free(ctx->results);
        free(ctx);
    }
}

static void gather_on_reject(MsVM* vm, void* userdata, MsValue error) {
    MsGatherCtx* ctx = (MsGatherCtx*)userdata;
    if (ctx->parent->state != MS_FUTURE_PENDING) return;
    ms_future_reject(vm, ctx->parent, error);
}

static MsValue native_time_gather(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "time.gather() requires a list argument.");
        return MS_NIL_VAL();
    }
    MsObjList* list = MS_AS_LIST(argv[0]);
    int n = list->items.count;
    MsObjFuture* parent = ms_obj_future_new(vm);
    if (n == 0) {
        MsObjList* empty = ms_obj_list_new(vm);
        ms_future_resolve(vm, parent, MS_OBJ_VAL((MsObject*)empty));
        return MS_OBJ_VAL((MsObject*)parent);
    }
    MsGatherCtx* ctx = (MsGatherCtx*)malloc(sizeof(MsGatherCtx));
    if (!ctx) { ms_vm_runtime_error(vm, "time.gather: out of memory."); return MS_NIL_VAL(); }
    ctx->parent    = parent;
    ctx->results   = (MsValue*)malloc(sizeof(MsValue) * (size_t)n);
    if (!ctx->results) {
        free(ctx);
        ms_vm_runtime_error(vm, "time.gather: out of memory.");
        return MS_NIL_VAL();
    }
    ctx->total     = n;
    ctx->remaining = n;
    for (int i = 0; i < n; i++) ctx->results[i] = MS_NIL_VAL();
    for (int i = 0; i < n; i++) {
        MsValue v = list->items.data[i];
        if (!MS_IS_FUTURE(v)) {
            free(ctx->results); free(ctx);
            ms_vm_runtime_error(vm, "time.gather: list must contain only Future objects.");
            return MS_NIL_VAL();
        }
        MsObjFuture* f = MS_AS_FUTURE(v);
        if (f->state == MS_FUTURE_RESOLVED) {
            gather_on_resolve(vm, ctx, i, f->result);
        } else if (f->state == MS_FUTURE_REJECTED) {
            gather_on_reject(vm, ctx, f->result);
            break;
        } else {
            ms_future_add_cb_waiter(vm, f, gather_on_resolve, gather_on_reject, ctx, i);
        }
    }
    return MS_OBJ_VAL((MsObject*)parent);
}

/* ---- time.resume(coroutine, value) ---- */

static MsValue native_time_resume(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_COROUTINE(argv[0])) {
        ms_vm_runtime_error(vm, "time.resume: expected coroutine as first argument.");
        return MS_NIL_VAL();
    }
    MsObjCoroutine* co = MS_AS_COROUTINE(argv[0]);
    MsValue sent = (argc >= 2) ? argv[1] : MS_NIL_VAL();
    MsValue result = MS_NIL_VAL();
    ms_vm_coro_resume(vm, co, sent, &result);
    return result;
}

/* ---- time.format(ts, fmt) ---- */

static MsValue native_time_format(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_NUMERIC(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "time.format: expected (num, string).");
        return MS_NIL_VAL();
    }
    time_t t = (time_t)ms_as_double(argv[0]);
    const char* fmt = MS_AS_CSTRING(argv[1]);
    struct tm tm_buf;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &t) != 0) {
        ms_vm_runtime_error(vm, "time.format: localtime failed.");
        return MS_NIL_VAL();
    }
#else
    if (!localtime_r(&t, &tm_buf)) {
        ms_vm_runtime_error(vm, "time.format: localtime failed.");
        return MS_NIL_VAL();
    }
#endif
    struct tm* tm_info = &tm_buf;
    char buf[256];
    strftime(buf, sizeof(buf), fmt, tm_info);
    return MS_OBJ_VAL((MsObject*)ms_obj_string_copy(vm, buf, (int)strlen(buf)));
}

/* ---- time.parse(text, fmt) ---- */

#ifdef _WIN32
/* Manual strptime for Windows: supports %Y %m %d %H %M %S %j.
 * Returns 1 on full match, 0 on format mismatch, -1 on unsupported specifier.
 * On -1, *bad_spec is set to the unsupported specifier character. */
static int win_strptime(const char* s, const char* fmt,
                        struct tm* tm_out, char* bad_spec) {
    const char* p = s;
    const char* f = fmt;
    while (*f) {
        if (*f != '%') {
            if (*p != *f) return 0;
            p++; f++;
            continue;
        }
        f++; /* skip '%' */
        char* end = NULL;
        long v;
        switch (*f) {
            case 'Y': v = strtol(p, &end, 10); if (end == p) return 0; tm_out->tm_year = (int)v - 1900; p = end; break;
            case 'm': v = strtol(p, &end, 10); if (end == p) return 0; tm_out->tm_mon  = (int)v - 1;    p = end; break;
            case 'd': v = strtol(p, &end, 10); if (end == p) return 0; tm_out->tm_mday = (int)v;        p = end; break;
            case 'H': v = strtol(p, &end, 10); if (end == p) return 0; tm_out->tm_hour = (int)v;        p = end; break;
            case 'M': v = strtol(p, &end, 10); if (end == p) return 0; tm_out->tm_min  = (int)v;        p = end; break;
            case 'S': v = strtol(p, &end, 10); if (end == p) return 0; tm_out->tm_sec  = (int)v;        p = end; break;
            case 'j': v = strtol(p, &end, 10); if (end == p) return 0; tm_out->tm_yday = (int)v - 1;   p = end; break;
            default:
                *bad_spec = *f;
                return -1; /* unsupported specifier */
        }
        f++;
    }
    /* Reject trailing text in input (cross-platform consistency with strptime) */
    return (*p == '\0') ? 1 : 0;
}
#endif

static MsValue native_time_parse(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "time.parse: expected (string, string).");
        return MS_NIL_VAL();
    }
    const char* text = MS_AS_CSTRING(argv[0]);
    const char* fmt  = MS_AS_CSTRING(argv[1]);
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));

#ifdef _WIN32
    char bad_spec = '?';
    int r = win_strptime(text, fmt, &tm_val, &bad_spec);
    if (r == -1) {
        char spec[3] = {'%', bad_spec, '\0'};
        ms_vm_runtime_error(vm, "time.parse: unsupported specifier '%s' on Windows", spec);
        return MS_NIL_VAL();
    }
    if (r == 0) {
        ms_vm_runtime_error(vm, "time.parse: invalid format");
        return MS_NIL_VAL();
    }
    tm_val.tm_isdst = 0;
    time_t t = mktime(&tm_val);
#else
    char* end = strptime(text, fmt, &tm_val);
    if (!end || *end != '\0') {
        ms_vm_runtime_error(vm, "time.parse: invalid format");
        return MS_NIL_VAL();
    }
    tm_val.tm_isdst = -1;
    time_t t = mktime(&tm_val);
#endif
    if (t == (time_t)-1) {
        ms_vm_runtime_error(vm, "time.parse: invalid format");
        return MS_NIL_VAL();
    }
    return MS_NUMBER_VAL((double)t);
}

/* ---- time.struct(ts) ---- */

static MsValue native_time_struct(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_NUMERIC(argv[0])) {
        ms_vm_runtime_error(vm, "time.struct: expected num.");
        return MS_NIL_VAL();
    }
    time_t t = (time_t)ms_as_double(argv[0]);
    struct tm tm_buf;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &t) != 0) {
        ms_vm_runtime_error(vm, "time.struct: localtime failed.");
        return MS_NIL_VAL();
    }
#else
    if (!localtime_r(&t, &tm_buf)) {
        ms_vm_runtime_error(vm, "time.struct: localtime failed.");
        return MS_NIL_VAL();
    }
#endif
    struct tm* tm_info = &tm_buf;
    MsObjMap* map = ms_obj_map_new(vm);

#define MAP_SET_INT(key, val) do { \
    MsObjString* _k = ms_obj_string_copy(vm, (key), (int)strlen(key)); \
    ms_vtable_set(&map->table, MS_OBJ_VAL(_k), MS_INT_VAL(val)); \
} while (0)

    MAP_SET_INT("year",  (ms_i64)(tm_info->tm_year + 1900));
    MAP_SET_INT("month", (ms_i64)(tm_info->tm_mon  + 1));
    MAP_SET_INT("day",   (ms_i64)tm_info->tm_mday);
    MAP_SET_INT("hour",  (ms_i64)tm_info->tm_hour);
    MAP_SET_INT("min",   (ms_i64)tm_info->tm_min);
    MAP_SET_INT("sec",   (ms_i64)tm_info->tm_sec);
    MAP_SET_INT("wday",  (ms_i64)tm_info->tm_wday);
    MAP_SET_INT("yday",  (ms_i64)tm_info->tm_yday);
    MAP_SET_INT("dst",   (ms_i64)tm_info->tm_isdst);

#undef MAP_SET_INT

    return MS_OBJ_VAL((MsObject*)map);
}

/* ---- time.sleep_sync(seconds) ---- */

static MsValue native_time_sleep_sync(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1 || !MS_IS_NUMERIC(argv[0])) {
        ms_vm_runtime_error(vm, "time.sleep_sync: expected num.");
        return MS_NIL_VAL();
    }
    double secs = ms_as_double(argv[0]);
    if (secs <= 0.0) return MS_NIL_VAL();
#ifdef _WIN32
    DWORD ms = (DWORD)(secs * 1000.0);
    Sleep(ms);
#else
    unsigned int us = (unsigned int)(secs * 1e6);
    usleep(us);
#endif
    return MS_NIL_VAL();
}

static const MsNativeDef time_defs[] = {
    { "now",                native_time_now,                0  },
    { "now_ms",             native_time_now_ms,             0  },
    { "monotonic_ms",       native_time_monotonic_ms,       0  },
    { "clock",              native_time_clock,              0  },
    { "format",             native_time_format,             2  },
    { "parse",              native_time_parse,              2  },
    { "struct",             native_time_struct,             1  },
    { "sleep",              native_time_sleep,              1  },
    { "sleep_sync",         native_time_sleep_sync,         1  },
    { "run_until_complete", native_time_run_until_complete, 1  },
    { "gather",             native_time_gather,             1  },
    { "resume",             native_time_resume,             -1 },
    { NULL, NULL, 0 }
};

void ms_module_time_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, time_defs);
}
