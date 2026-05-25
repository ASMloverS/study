/* src/stdlib/_rand.c - STDLIB-24: _rand C primitive module
 *
 * MT19937 PRNG. Global singleton state stored in MsObjUserdata.
 * OS entropy seed via /dev/urandom (POSIX) or BCryptGenRandom (Windows).
 *
 * Exports:
 *   _rand.seed(n)           → nil  (nil=OS entropy, int=explicit seed)
 *   _rand.rand_int(lo, hi)  → int  ([lo, hi] closed, rejection sampling)
 *   _rand.rand_float()      → num  ([0.0, 1.0))
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <bcrypt.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* MT19937                                                              */
/* ------------------------------------------------------------------ */

#define MT_N   624
#define MT_M   397
#define MT_MAT 0x9908b0dfUL

typedef struct {
    uint32_t mt[MT_N];
    int      idx;      /* next index to generate from; MT_N+1 = uninitialized */
} MtState;

static void mt_seed_u32(MtState* s, uint32_t seed) {
    s->mt[0] = seed;
    for (int i = 1; i < MT_N; i++)
        s->mt[i] = 1812433253UL * (s->mt[i-1] ^ (s->mt[i-1] >> 30)) + (uint32_t)i;
    s->idx = MT_N;
}

static void mt_generate(MtState* s) {
    static const uint32_t mag[2] = {0, MT_MAT};
    for (int i = 0; i < MT_N - MT_M; i++) {
        uint32_t y = (s->mt[i] & 0x80000000UL) | (s->mt[i+1] & 0x7fffffffUL);
        s->mt[i] = s->mt[i + MT_M] ^ (y >> 1) ^ mag[y & 1];
    }
    for (int i = MT_N - MT_M; i < MT_N - 1; i++) {
        uint32_t y = (s->mt[i] & 0x80000000UL) | (s->mt[i+1] & 0x7fffffffUL);
        s->mt[i] = s->mt[i + (MT_M - MT_N)] ^ (y >> 1) ^ mag[y & 1];
    }
    uint32_t y = (s->mt[MT_N-1] & 0x80000000UL) | (s->mt[0] & 0x7fffffffUL);
    s->mt[MT_N-1] = s->mt[MT_M-1] ^ (y >> 1) ^ mag[y & 1];
    s->idx = 0;
}

static uint32_t mt_next_u32(MtState* s) {
    if (s->idx >= MT_N) mt_generate(s);
    uint32_t y = s->mt[s->idx++];
    y ^= (y >> 11);
    y ^= (y << 7)  & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);
    return y;
}

/* [0.0, 1.0) with 53-bit precision */
static double mt_next_double(MtState* s) {
    uint64_t a = ((uint64_t)mt_next_u32(s)) >> 5;
    uint64_t b = ((uint64_t)mt_next_u32(s)) >> 6;
    return (double)(a * 67108864ULL + b) * (1.0 / 9007199254740992.0);
}

/* ------------------------------------------------------------------ */
/* OS entropy                                                           */
/* ------------------------------------------------------------------ */

static bool os_entropy(uint8_t* buf, size_t len) {
#ifdef _WIN32
    return BCRYPT_SUCCESS(BCryptGenRandom(NULL, buf, (ULONG)len,
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, len);
    close(fd);
    return n == (ssize_t)len;
#endif
}

/* ------------------------------------------------------------------ */
/* Global singleton: stored as a module-level userdata export          */
/* ------------------------------------------------------------------ */

#define RAND_TAG "_rand_state"

/* Finalize: nothing heap-allocated inside MtState (it's inline in userdata) */
static void rand_finalize(void* data) { (void)data; }

static MtState* get_state(MsVM* vm, MsObjModule* mod) {
    MsObjString* key = ms_obj_string_copy(vm, "_state", 6);
    MsValue v;
    if (!ms_table_get(&mod->exports, key, &v)) return NULL;
    if (!MS_IS_USERDATA(v)) return NULL;
    MsObjUserdata* ud = MS_AS_USERDATA(v);
    if (strcmp(ud->type_tag, RAND_TAG) != 0) return NULL;
    return (MtState*)ud->data;
}

/* ------------------------------------------------------------------ */
/* Native functions                                                     */
/* ------------------------------------------------------------------ */

/* _rand.seed(n) */
static MsValue rand_seed(MsVM* vm, int argc, MsValue* argv) {
    /* find the module to get the state */
    MsObjString* mkey = ms_obj_string_copy(vm, "<builtin:_rand>", 15);
    MsValue modv;
    if (!ms_table_get(&vm->module_cache, mkey, &modv) || !MS_IS_MODULE(modv)) {
        ms_vm_runtime_error(vm, "_rand.seed: module not found");
        return MS_NIL_VAL();
    }
    MsObjModule* mod = MS_AS_MODULE(modv);
    MtState* s = get_state(vm, mod);
    if (!s) {
        ms_vm_runtime_error(vm, "_rand.seed: state not initialized");
        return MS_NIL_VAL();
    }

    if (argc == 0 || MS_IS_NIL(argv[0])) {
        /* OS entropy */
        uint32_t seed_buf[MT_N];
        if (os_entropy((uint8_t*)seed_buf, sizeof(seed_buf))) {
            memcpy(s->mt, seed_buf, sizeof(seed_buf));
            s->idx = MT_N;
        } else {
            /* fallback: time-based */
            mt_seed_u32(s, (uint32_t)(uintptr_t)s);
        }
    } else {
        ms_i64 n = MS_IS_INT(argv[0]) ? MS_AS_INT(argv[0])
                                       : (ms_i64)ms_as_double(argv[0]);
        mt_seed_u32(s, (uint32_t)(uint64_t)n);
    }
    return MS_NIL_VAL();
}

/* _rand.rand_float() */
static MsValue rand_float_fn(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    MsObjString* mkey = ms_obj_string_copy(vm, "<builtin:_rand>", 15);
    MsValue modv;
    if (!ms_table_get(&vm->module_cache, mkey, &modv) || !MS_IS_MODULE(modv)) {
        ms_vm_runtime_error(vm, "_rand.rand_float: module not found");
        return MS_NIL_VAL();
    }
    MtState* s = get_state(vm, MS_AS_MODULE(modv));
    if (!s) {
        ms_vm_runtime_error(vm, "_rand.rand_float: state not initialized");
        return MS_NIL_VAL();
    }
    return MS_NUMBER_VAL(mt_next_double(s));
}

/* _rand.rand_int(lo, hi) — closed [lo, hi], rejection sampling */
static MsValue rand_int_fn(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_INT(argv[0]) || !MS_IS_INT(argv[1])) {
        ms_vm_runtime_error(vm, "_rand.rand_int: expected (int, int)");
        return MS_NIL_VAL();
    }
    ms_i64 lo = MS_AS_INT(argv[0]);
    ms_i64 hi = MS_AS_INT(argv[1]);
    if (lo > hi) {
        ms_vm_runtime_error(vm, "_rand.rand_int: lo (%lld) > hi (%lld)",
                            (long long)lo, (long long)hi);
        return MS_NIL_VAL();
    }
    if (lo == hi) return MS_INT_VAL(lo);

    MsObjString* mkey = ms_obj_string_copy(vm, "<builtin:_rand>", 15);
    MsValue modv;
    if (!ms_table_get(&vm->module_cache, mkey, &modv) || !MS_IS_MODULE(modv)) {
        ms_vm_runtime_error(vm, "_rand.rand_int: module not found");
        return MS_NIL_VAL();
    }
    MtState* s = get_state(vm, MS_AS_MODULE(modv));
    if (!s) {
        ms_vm_runtime_error(vm, "_rand.rand_int: state not initialized");
        return MS_NIL_VAL();
    }

    /* Unbiased rejection sampling over [lo, hi] */
    uint64_t range = (uint64_t)((uint64_t)(uint64_t)(hi - lo) + 1ULL);
    /* Compute threshold: smallest multiple of range that fits in 2^32 space */
    uint64_t threshold = (uint64_t)(-(int64_t)range) % range; /* = 2^64 % range */
    uint64_t raw;
    do {
        raw = ((uint64_t)mt_next_u32(s) << 32) | (uint64_t)mt_next_u32(s);
    } while (raw < threshold);
    return MS_INT_VAL(lo + (ms_i64)(raw % range));
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

void ms_module_rand_init(MsVM* vm, MsObjModule* mod) {
    /* allocate MT state as userdata — GC managed */
    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(MtState),
                                             rand_finalize, NULL, RAND_TAG);
    MtState* s = (MtState*)ud->data;
    /* default seed: OS entropy */
    uint32_t seed_buf[MT_N];
    if (os_entropy((uint8_t*)seed_buf, sizeof(seed_buf))) {
        memcpy(s->mt, seed_buf, sizeof(seed_buf));
        s->idx = MT_N;
    } else {
        mt_seed_u32(s, 12345UL); /* deterministic fallback */
    }

    /* export state so native fns can find it */
    ms_module_export_value(vm, mod, "_state", MS_OBJ_VAL(ud));

    static const MsNativeDef defs[] = {
        {"seed",       rand_seed,     -1},
        {"rand_float", rand_float_fn,  0},
        {"rand_int",   rand_int_fn,    2},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);
}
