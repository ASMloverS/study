#include "ms/common.h"
#include "ms/consts.h"
#include "ms/vm.h"
#include "ms/serializer.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stdout redirect (json mode: silence script output during runs) ---- */

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  define MS_DEVNULL "NUL"
#  define ms_dup(fd)        _dup(fd)
#  define ms_dup2(src, dst) _dup2(src, dst)
#  define ms_close(fd)      _close(fd)
#  define ms_fileno(f)      _fileno(f)
#else
#  include <unistd.h>
#  define MS_DEVNULL "/dev/null"
#  define ms_dup(fd)        dup(fd)
#  define ms_dup2(src, dst) dup2(src, dst)
#  define ms_close(fd)      close(fd)
#  define ms_fileno(f)      fileno(f)
#endif

/* Redirect stdout to MS_DEVNULL so VM print() is suppressed.
   Returns saved fd (>= 0) on success, -1 on failure. */
static int stdout_silence(void) {
    fflush(stdout);
    int saved = ms_dup(ms_fileno(stdout));
    if (saved < 0) return -1;
#ifdef _MSC_VER
    FILE* nul = NULL;
    if (freopen_s(&nul, MS_DEVNULL, "wb", stdout) != 0) {
        ms_close(saved); return -1;
    }
    MS_UNUSED(nul);
#else
    if (!freopen(MS_DEVNULL, "wb", stdout)) {
        ms_close(saved); return -1;
    }
#endif
    return saved;
}

/* Restore stdout from the fd saved by stdout_silence(). */
static void stdout_restore(int saved) {
    if (saved < 0) return;
    fflush(stdout);
    ms_dup2(saved, ms_fileno(stdout));
    ms_close(saved);
}

/* ---- platform timing ---- */

#ifdef _WIN32
static double get_time_ms(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#  include <time.h>
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
#endif

/* ---- file helpers ---- */

static char* read_file(const char* path) {
    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f) { fprintf(stderr, "Cannot open file '%s'.\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

static bool has_msc_ext(const char* path) {
    size_t n = strlen(path);
    return n >= 4 && strcmp(path + n - 4, ".msc") == 0;
}

/* ---- sort helpers for median ---- */

static int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

static double median_of(double* arr, int n) {
    qsort(arr, (size_t)n, sizeof(double), cmp_double);
    if (n % 2 == 1) return arr[n / 2];
    return (arr[n / 2 - 1] + arr[n / 2]) / 2.0;
}

/* ---- benchmark run ---- */

typedef struct {
    double compile_ms;
    double interpret_ms;
} RunSample;

/* Run one iteration: compile fresh from source, then interpret.
   Returns MS_INTERPRET_OK on success.
   If stats_out is non-NULL (MSLANG_VM_STATS only), copies stats before freeing. */
static MsInterpretResult run_one_nocache(const char* src, const char* path,
                                          RunSample* s
#ifdef MSLANG_VM_STATS
                                          , MsVMStats* stats_out
#endif
                                          ) {
    double t1, t2;

    /* Heap-allocate MsVM to avoid stack overflow (~300 KB struct) and
       to ensure clean state each iteration regardless of compiler inlining. */
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) { return MS_INTERPRET_RUNTIME_ERROR; }
    ms_vm_init(vm);

    t1 = get_time_ms();
    MsInterpretResult res = ms_vm_interpret(vm, src, path);
    t2 = get_time_ms();

    s->compile_ms   = 0.0;
    s->interpret_ms = t2 - t1;
#ifdef MSLANG_VM_STATS
    if (stats_out) {
        int live = 0;
        for (MsObject* o = vm->young_objects; o; o = o->next) live++;
        for (MsObject* o = vm->old_objects;   o; o = o->next) live++;
        for (MsObject* o = vm->objects;       o; o = o->next) live++;
        vm->stats.live_objects_after_final_gc = live;
        ms_vm_get_stats(vm, stats_out);
    }
#endif
    ms_vm_free(vm);
    free(vm);
    return res;
}

/* Run one iteration using ms_compile_cached (with-cache mode).
   cache_flags: MS_CACHE_MTIME (0) or MS_CACHE_HASH (1).
   If stats_out is non-NULL (MSLANG_VM_STATS only), copies stats before freeing. */
static MsInterpretResult run_one_cached(const char* path, uint32_t cache_flags,
                                         RunSample* s
#ifdef MSLANG_VM_STATS
                                         , MsVMStats* stats_out
#endif
                                         ) {
    double t0, t1, t2;
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) { return MS_INTERPRET_RUNTIME_ERROR; }
    ms_vm_init(vm);

    t0 = get_time_ms();
    MsObjFunction* fn = ms_compile_cached(vm, path, cache_flags);
    t1 = get_time_ms();
    s->compile_ms = t1 - t0;

    MsInterpretResult res = MS_INTERPRET_OK;
    if (!fn) {
        res = MS_INTERPRET_COMPILE_ERROR;
    } else {
        MsObjClosure* cl = ms_obj_closure_new(vm, fn);
        vm->ctx->frames[0].closure = cl;
        vm->ctx->frames[0].ip      = fn->chunk.code;
        vm->ctx->frames[0].slots   = vm->ctx->stack;
        vm->ctx->frame_count = 1;
        int need = fn->max_stack_size + 1;
        if (need < 1) need = 1;
        vm->ctx->stack_top = vm->ctx->stack + need;
        /* fn is now rooted via frames[0]; safe to allocate script_path string */
        if (!fn->script_path)
            fn->script_path = ms_obj_string_copy(vm, path, (int)strlen(path));
        double r0 = get_time_ms();
        res = ms_vm_run(vm);
        t2 = get_time_ms();
        MS_UNUSED(r0);
        s->interpret_ms = t2 - t1;
    }
#ifdef MSLANG_VM_STATS
    if (stats_out && res == MS_INTERPRET_OK) {
        int live = 0;
        for (MsObject* o = vm->young_objects; o; o = o->next) live++;
        for (MsObject* o = vm->old_objects;   o; o = o->next) live++;
        for (MsObject* o = vm->objects;       o; o = o->next) live++;
        vm->stats.live_objects_after_final_gc = live;
        ms_vm_get_stats(vm, stats_out);
    }
#endif
    ms_vm_free(vm);
    free(vm);
    return res;
}

static MsInterpretResult run_one_msc(const char* path, RunSample* s
#ifdef MSLANG_VM_STATS
                                      , MsVMStats* stats_out
#endif
                                      ) {
    double t0, t1, t2;
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) { return MS_INTERPRET_RUNTIME_ERROR; }
    ms_vm_init(vm);

    t0 = get_time_ms();
    MsObjFunction* fn = ms_load_msc(vm, path);
    t1 = get_time_ms();
    s->compile_ms = t1 - t0;

    MsInterpretResult res = MS_INTERPRET_OK;
    if (!fn) {
        res = MS_INTERPRET_COMPILE_ERROR;
    } else {
        MsObjClosure* cl = ms_obj_closure_new(vm, fn);
        vm->ctx->frames[0].closure = cl;
        vm->ctx->frames[0].ip      = fn->chunk.code;
        vm->ctx->frames[0].slots   = vm->ctx->stack;
        vm->ctx->frame_count = 1;
        int need = fn->max_stack_size + 1;
        if (need < 1) need = 1;
        vm->ctx->stack_top = vm->ctx->stack + need;
        if (!fn->script_path)
            fn->script_path = ms_obj_string_copy(vm, path, (int)strlen(path));
        res = ms_vm_run(vm);
        t2 = get_time_ms();
        s->interpret_ms = t2 - t1;
    }
#ifdef MSLANG_VM_STATS
    if (stats_out && res == MS_INTERPRET_OK) {
        int live = 0;
        for (MsObject* o = vm->young_objects; o; o = o->next) live++;
        for (MsObject* o = vm->old_objects;   o; o = o->next) live++;
        for (MsObject* o = vm->objects;       o; o = o->next) live++;
        vm->stats.live_objects_after_final_gc = live;
        ms_vm_get_stats(vm, stats_out);
    }
#endif
    ms_vm_free(vm);
    free(vm);
    return res;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("mslang-c %s\n", MS_VERSION);
        return 0;
    }

    /* Parse flags */
    int   bench_n       = 0;
    bool  flag_stats    = false;
    bool  flag_json     = false;
    bool  flag_no_cache = false;           /* --no-cache */
    uint32_t cache_flags = MS_CACHE_MTIME; /* --cache-mode=mtime|hash */
    const char* script = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--benchmark") == 0 && i + 1 < argc) {
            bench_n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--stats") == 0) {
            flag_stats = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            flag_json = true;
        } else if (strcmp(argv[i], "--no-cache") == 0) {
            flag_no_cache = true;
        } else if (strncmp(argv[i], "--cache-mode=", 13) == 0) {
            const char* mode = argv[i] + 13;
            if (strcmp(mode, "hash") == 0)
                cache_flags = MS_CACHE_HASH;
            else if (strcmp(mode, "mtime") == 0)
                cache_flags = MS_CACHE_MTIME;
            else {
                fprintf(stderr, "Unknown cache mode: %s (use mtime or hash)\n", mode);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            script = argv[i];
        } else {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            return 1;
        }
    }

    if (!script) {
        fprintf(stderr,
            "Usage: mslang-c [--benchmark N] [--stats] [--json]\n"
            "                [--no-cache] [--cache-mode=mtime|hash]\n"
            "                [--version] <script>\n");
        return 1;
    }

    /* Only read source for --no-cache or benchmark no-cache path */
    char* src = NULL;
    if (flag_no_cache) {
        src = read_file(script);
        if (!src) return 1;
    }

    /* Non-benchmark: simple run */
    if (bench_n <= 0) {
        MsVM vm;
        ms_vm_init(&vm);
        MsInterpretResult result;

        if (has_msc_ext(script)) {
            /* Direct .msc execution: load bytecode, skip source validation */
            MsObjFunction* fn = ms_load_msc(&vm, script);
            if (!fn) {
                ms_vm_free(&vm);
                free(src);
                return 1;
            }
            MsObjClosure* cl = ms_obj_closure_new(&vm, fn);
            vm.ctx->frames[0].closure = cl;
            vm.ctx->frames[0].ip      = fn->chunk.code;
            vm.ctx->frames[0].slots   = vm.ctx->stack;
            vm.ctx->frame_count       = 1;
            int need = fn->max_stack_size + 1;
            if (need < 1) need = 1;
            vm.ctx->stack_top = vm.ctx->stack + need;
            if (!fn->script_path)
                fn->script_path = ms_obj_string_copy(&vm, script, (int)strlen(script));
            result = ms_vm_run(&vm);
        } else if (!flag_no_cache) {
            /* Default: cache path - source file not read by us */
            MsObjFunction* fn = ms_compile_cached(&vm, script, cache_flags);
            if (!fn) {
                ms_vm_free(&vm);
                free(src);
                return 1;
            }
            MsObjClosure* cl = ms_obj_closure_new(&vm, fn);
            vm.ctx->frames[0].closure = cl;
            vm.ctx->frames[0].ip      = fn->chunk.code;
            vm.ctx->frames[0].slots   = vm.ctx->stack;
            vm.ctx->frame_count       = 1;
            int need = fn->max_stack_size + 1;
            if (need < 1) need = 1;
            vm.ctx->stack_top = vm.ctx->stack + need;
            /* fn is now rooted via frames[0]; safe to allocate script_path string */
            if (!fn->script_path)
                fn->script_path = ms_obj_string_copy(&vm, script, (int)strlen(script));
            result = ms_vm_run(&vm);
        } else {
            /* --no-cache: classic interpret path (needs source) */
            if (!src) { src = read_file(script); }
            if (!src) { ms_vm_free(&vm); return 1; }
            result = ms_vm_interpret(&vm, src, script);
        }

#ifdef MSLANG_VM_STATS
        if (flag_stats && result == MS_INTERPRET_OK) {
            MsVMStats st;
            ms_vm_get_stats(&vm, &st);
            fprintf(stderr, "stats: instr=%" PRIu64 " minor_gc=%" PRIu64
                    " major_gc=%" PRIu64 " incr_step=%" PRIu64
                    " deopt=%" PRIu64 " peak_bytes=%zu peak_frames=%d"
                    " live_obj=%d\n",
                    st.instruction_count, st.minor_gc_count, st.major_gc_count,
                    st.incremental_step_count, st.deopt_event_count,
                    st.bytes_allocated_peak, st.peak_frame_count,
                    st.live_objects_after_final_gc);
        }
#else
        MS_UNUSED(flag_stats);
#endif
        MS_UNUSED(flag_json);
        ms_vm_free(&vm);
        free(src);
        return result == MS_INTERPRET_OK ? 0 : 1;
    }

    /* Benchmark mode */
    double* compile_times   = (double*)malloc(sizeof(double) * (size_t)bench_n);
    double* interpret_times = (double*)malloc(sizeof(double) * (size_t)bench_n);
    double  compile_cold    = 0.0;
    if (!compile_times || !interpret_times) { free(src); return 1; }

#ifdef MSLANG_VM_STATS
    MsVMStats last_stats;
    memset(&last_stats, 0, sizeof(last_stats));
#endif

    for (int i = 0; i < bench_n; i++) {
        RunSample s = {0.0, 0.0};
        MsInterpretResult res;

        /* In JSON mode, silence script stdout so JSON is the only stdout line */
        int saved_fd = flag_json ? stdout_silence() : -1;

        /* Capture stats from the last benchmark run to avoid a separate VM. */
#ifdef MSLANG_VM_STATS
        bool is_last = (i == bench_n - 1);
        MsVMStats* stats_capture = (flag_stats && is_last) ? &last_stats : NULL;
        if (has_msc_ext(script)) {
            res = run_one_msc(script, &s, stats_capture);
            if (i == 0) compile_cold = s.compile_ms;
        } else if (!flag_no_cache) {
            res = run_one_cached(script, cache_flags, &s, stats_capture);
            if (i == 0) compile_cold = s.compile_ms;
        } else {
            if (!src) { src = read_file(script); if (!src) { free(compile_times); free(interpret_times); return 1; } }
            res = run_one_nocache(src, script, &s, stats_capture);
        }
#else
        if (has_msc_ext(script)) {
            res = run_one_msc(script, &s);
            if (i == 0) compile_cold = s.compile_ms;
        } else if (!flag_no_cache) {
            res = run_one_cached(script, cache_flags, &s);
            if (i == 0) compile_cold = s.compile_ms;
        } else {
            if (!src) { src = read_file(script); if (!src) { free(compile_times); free(interpret_times); return 1; } }
            res = run_one_nocache(src, script, &s);
        }
#endif

        if (flag_json) stdout_restore(saved_fd);

        if (res != MS_INTERPRET_OK) {
            free(compile_times); free(interpret_times); free(src);
            return 1;
        }

        compile_times[i]   = s.compile_ms;
        interpret_times[i] = s.interpret_ms;

        if (!flag_json) {
            printf("run %d: compile=%.3f ms  interpret=%.3f ms\n",
                   i + 1, s.compile_ms, s.interpret_ms);
        }
    }

    /* Aggregate */
    double* itimes_copy = (double*)malloc(sizeof(double) * (size_t)bench_n);
    if (!itimes_copy) { free(compile_times); free(interpret_times); free(src); return 1; }
    memcpy(itimes_copy, interpret_times, sizeof(double) * (size_t)bench_n);

    double min_ms = interpret_times[0], max_ms = interpret_times[0], sum = 0.0;
    for (int i = 0; i < bench_n; i++) {
        if (interpret_times[i] < min_ms) min_ms = interpret_times[i];
        if (interpret_times[i] > max_ms) max_ms = interpret_times[i];
        sum += interpret_times[i];
    }
    double mean_ms   = sum / bench_n;
    double med_ms    = median_of(itimes_copy, bench_n);
    double compile_warm = 0.0;
    if (bench_n > 1 && !flag_no_cache) {
        compile_warm = compile_times[1];
        for (int i = 2; i < bench_n; i++) {
            if (compile_times[i] < compile_warm) compile_warm = compile_times[i];
        }
    }

    if (flag_json) {
        printf("{\"runs\":%d,\"best_ms\":%.3f,\"median_ms\":%.3f,"
               "\"max_ms\":%.3f,\"mean_ms\":%.3f",
               bench_n, min_ms, med_ms, max_ms, mean_ms);
        if (!flag_no_cache) {
            printf(",\"compile_ms_cold\":%.3f,\"compile_ms_warm\":%.3f",
                   compile_cold, compile_warm);
        }
#ifdef MSLANG_VM_STATS
        if (flag_stats) {
            printf(",\"instruction_count\":%" PRIu64
                   ",\"minor_gc_count\":%" PRIu64
                   ",\"major_gc_count\":%" PRIu64
                   ",\"incremental_step_count\":%" PRIu64
                   ",\"deopt_event_count\":%" PRIu64
                   ",\"bytes_allocated_peak\":%zu"
                   ",\"peak_frame_count\":%d"
                   ",\"live_objects_after_final_gc\":%d",
                   last_stats.instruction_count, last_stats.minor_gc_count,
                   last_stats.major_gc_count, last_stats.incremental_step_count,
                   last_stats.deopt_event_count, last_stats.bytes_allocated_peak,
                   last_stats.peak_frame_count,
                   last_stats.live_objects_after_final_gc);
        }
#endif
        printf("}\n");
    } else {
        printf("---\n");
        printf("runs=%d  best=%.3f ms  median=%.3f ms  max=%.3f ms  mean=%.3f ms\n",
               bench_n, min_ms, med_ms, max_ms, mean_ms);
        if (!flag_no_cache) {
            printf("compile_cold=%.3f ms  compile_warm=%.3f ms\n",
                   compile_cold, compile_warm);
        }
#ifdef MSLANG_VM_STATS
        if (flag_stats) {
            printf("instruction_count=%" PRIu64
                   "  minor_gc=%" PRIu64
                   "  major_gc=%" PRIu64
                   "  incr_step=%" PRIu64
                   "  deopt=%" PRIu64
                   "  peak_bytes=%zu"
                   "  peak_frames=%d"
                   "  live_obj=%d\n",
                   last_stats.instruction_count, last_stats.minor_gc_count,
                   last_stats.major_gc_count, last_stats.incremental_step_count,
                   last_stats.deopt_event_count, last_stats.bytes_allocated_peak,
                   last_stats.peak_frame_count,
                   last_stats.live_objects_after_final_gc);
        }
#endif
    }

    free(compile_times);
    free(interpret_times);
    free(itimes_copy);
    free(src);
    return 0;
}
