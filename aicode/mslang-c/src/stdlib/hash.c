/* src/stdlib/hash.c - STDLIB-06: hash module
 * Provides: md5, sha1, sha256 (one-shot + streaming Hasher),
 *           crc32, fnv1a.
 */
#include "ms/stdlib_register.h"
#include "ms/stdlib/hash_impl.h"
#include "ms/stdlib/objbuffer.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include <string.h>
#include <stdint.h>

/* ================================================================
 * Hasher userdata
 * ================================================================ */

#define ALGO_MD5    0
#define ALGO_SHA1   1
#define ALGO_SHA256 2

typedef struct {
    int  algo;
    bool finalized;
    union {
        MD5_CTX    md5;
        SHA1_CTX   sha1;
        SHA256_CTX sha256;
    } ctx;
} MsHasherData;

static void hasher_finalize(void* data) {
    (void)data; /* inline storage — nothing to free */
}

static void hasher_ctx_init(MsHasherData* h) {
    switch (h->algo) {
        case ALGO_MD5:    ms_md5_init   (&h->ctx.md5);    break;
        case ALGO_SHA1:   ms_sha1_init  (&h->ctx.sha1);   break;
        case ALGO_SHA256: ms_sha256_init(&h->ctx.sha256); break;
    }
}

static int parse_algo(const char* s) {
    if (strcmp(s, "md5")    == 0) return ALGO_MD5;
    if (strcmp(s, "sha1")   == 0) return ALGO_SHA1;
    if (strcmp(s, "sha256") == 0) return ALGO_SHA256;
    return -1;
}

/* ================================================================
 * Helper: coerce str|Buffer → raw bytes
 * ================================================================ */

static bool coerce_bytes(MsValue v, const uint8_t** out, size_t* len) {
    if (MS_IS_STRING(v)) {
        MsObjString* s = MS_AS_STRING(v);
        *out = (const uint8_t*)s->data;
        *len = (size_t)s->length;
        return true;
    }
    if (MS_IS_BUFFER(v)) {
        MsObjBuffer* b = MS_AS_BUFFER(v);
        *out = b->data;
        *len = b->len;
        return true;
    }
    return false;
}

/* ================================================================
 * One-shot functions
 * ================================================================ */

static MsValue ms_hash_md5(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "hash.md5: expected str or Buffer");
        return MS_NIL_VAL();
    }
    uint8_t digest[16];
    ms_md5_oneshot(data, len, digest);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, digest, 16));
}

static MsValue ms_hash_sha1(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "hash.sha1: expected str or Buffer");
        return MS_NIL_VAL();
    }
    uint8_t digest[20];
    ms_sha1_oneshot(data, len, digest);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, digest, 20));
}

static MsValue ms_hash_sha256(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "hash.sha256: expected str or Buffer");
        return MS_NIL_VAL();
    }
    uint8_t digest[32];
    ms_sha256_oneshot(data, len, digest);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, digest, 32));
}

static MsValue ms_hash_crc32(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "hash.crc32: expected str or Buffer");
        return MS_NIL_VAL();
    }
    uint32_t crc = ms_crc32(data, len);
    return MS_INT_VAL((ms_i64)(uint32_t)crc);
}

/* hash.fnv1a(data, bits=64) */
static MsValue ms_hash_fnv1a(MsVM* vm, int argc, MsValue* argv) {
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "hash.fnv1a: expected str or Buffer");
        return MS_NIL_VAL();
    }
    int bits = 64;
    if (argc >= 2 && MS_IS_INT(argv[1])) bits = (int)MS_AS_INT(argv[1]);
    if (bits != 32 && bits != 64) {
        ms_vm_runtime_error(vm, "hash.fnv1a: bits must be 32 or 64");
        return MS_NIL_VAL();
    }
    if (bits == 32) return MS_INT_VAL((ms_i64)ms_fnv1a_32(data, len));
    return MS_INT_VAL((ms_i64)ms_fnv1a_64(data, len));
}

/* ================================================================
 * Streaming Hasher (v1 functional API)
 * ================================================================ */

static bool is_hasher(MsValue v) {
    return MS_IS_USERDATA(v) &&
           strcmp(MS_AS_USERDATA(v)->type_tag, "MsHasher") == 0;
}

/* hash.new(algo) → Hasher */
static MsValue ms_hash_new(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "hash.new: expected string algo name");
        return MS_NIL_VAL();
    }
    int algo = parse_algo(MS_AS_CSTRING(argv[0]));
    if (algo < 0) {
        ms_vm_runtime_error(vm, "hash.new: unknown algo '%s' (use \"md5\", \"sha1\", \"sha256\")",
                            MS_AS_CSTRING(argv[0]));
        return MS_NIL_VAL();
    }
    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(MsHasherData),
                                            hasher_finalize, NULL, "MsHasher");
    MsHasherData* h = (MsHasherData*)ud->data;
    h->algo      = algo;
    h->finalized = false;
    hasher_ctx_init(h);
    return MS_OBJ_VAL(ud);
}

/* hash.update(h, data) */
static MsValue ms_hash_update(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_hasher(argv[0])) {
        ms_vm_runtime_error(vm, "hash.update: expected Hasher");
        return MS_NIL_VAL();
    }
    MsHasherData* h = (MsHasherData*)MS_AS_USERDATA(argv[0])->data;
    if (h->finalized) {
        ms_vm_runtime_error(vm, "hash: hasher already finalized, call reset() first");
        return MS_NIL_VAL();
    }
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[1], &data, &len)) {
        ms_vm_runtime_error(vm, "hash.update: expected str or Buffer");
        return MS_NIL_VAL();
    }
    switch (h->algo) {
        case ALGO_MD5:    ms_md5_update   (&h->ctx.md5,    data, len); break;
        case ALGO_SHA1:   ms_sha1_update  (&h->ctx.sha1,   data, len); break;
        case ALGO_SHA256: ms_sha256_update(&h->ctx.sha256, data, len); break;
    }
    return MS_NIL_VAL();
}

/* hash.digest(h) → Buffer */
static MsValue ms_hash_digest(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_hasher(argv[0])) {
        ms_vm_runtime_error(vm, "hash.digest: expected Hasher");
        return MS_NIL_VAL();
    }
    MsHasherData* h = (MsHasherData*)MS_AS_USERDATA(argv[0])->data;
    if (h->finalized) {
        ms_vm_runtime_error(vm, "hash: hasher already finalized, call reset() first");
        return MS_NIL_VAL();
    }
    h->finalized = true;
    switch (h->algo) {
        case ALGO_MD5: {
            uint8_t d[16];
            ms_md5_final(&h->ctx.md5, d);
            return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, d, 16));
        }
        case ALGO_SHA1: {
            uint8_t d[20];
            ms_sha1_final(&h->ctx.sha1, d);
            return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, d, 20));
        }
        case ALGO_SHA256: {
            uint8_t d[32];
            ms_sha256_final(&h->ctx.sha256, d);
            return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, d, 32));
        }
    }
    return MS_NIL_VAL();
}

/* hash.hexdigest(h) → str */
static MsValue ms_hash_hexdigest(MsVM* vm, int argc, MsValue* argv) {
    MsValue buf = ms_hash_digest(vm, argc, argv);
    if (vm->had_runtime_error || !MS_IS_BUFFER(buf)) return MS_NIL_VAL();
    MsObjBuffer* b = MS_AS_BUFFER(buf);
    /* build hex string */
    MsValue out;
    MsValue bval = MS_OBJ_VAL(b);
    if (!ms_objbuffer_invoke(vm, b,
            ms_obj_string_copy(vm, "to_hex", 6),
            0, &bval, &out)) {
        return MS_NIL_VAL();
    }
    return out;
}

/* hash.reset(h) */
static MsValue ms_hash_reset(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_hasher(argv[0])) {
        ms_vm_runtime_error(vm, "hash.reset: expected Hasher");
        return MS_NIL_VAL();
    }
    MsHasherData* h = (MsHasherData*)MS_AS_USERDATA(argv[0])->data;
    h->finalized = false;
    hasher_ctx_init(h);
    return MS_NIL_VAL();
}

/* ================================================================
 * Registration
 * ================================================================ */

static const MsNativeDef ms_hash_defs[] = {
    {"md5",       ms_hash_md5,      1},
    {"sha1",      ms_hash_sha1,     1},
    {"sha256",    ms_hash_sha256,   1},
    {"crc32",     ms_hash_crc32,    1},
    {"fnv1a",     ms_hash_fnv1a,   -1},
    {"new",       ms_hash_new,      1},
    {"update",    ms_hash_update,   2},
    {"digest",    ms_hash_digest,   1},
    {"hexdigest", ms_hash_hexdigest,1},
    {"reset",     ms_hash_reset,    1},
    {NULL, NULL, 0}
};

void ms_module_hash_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_hash_defs);
}
