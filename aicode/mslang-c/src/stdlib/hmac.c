/* src/stdlib/hmac.c - STDLIB-44: hmac module
 *
 * HMAC (RFC 2104): HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))
 * where K' = H(K) if |K| > block_size, else K padded with 0x00.
 *
 * Supports: md5 (block=64), sha1 (block=64), sha256 (block=64).
 *
 * Public API:
 *   hmac.compute(algo, key, data) -> str  one-shot hexdigest
 *   hmac.new(algo, key)           -> Hmac streaming object
 *   h.update(data)                -> nil
 *   h.hexdigest()                 -> str  (idempotent)
 *   h.digest()                    -> buffer
 */
#include "ms/stdlib/hash_impl.h"
#include "ms/stdlib/objbuffer.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* ================================================================
 * Algorithm IDs and block sizes
 * ================================================================ */

#define HMAC_ALGO_MD5    0
#define HMAC_ALGO_SHA1   1
#define HMAC_ALGO_SHA256 2

#define HMAC_BLOCK_SIZE  64  /* all three algos use 64-byte blocks */
#define HMAC_MAX_DIGEST  32  /* sha256 is the largest */

static int hmac_parse_algo(const char* s) {
    if (strcmp(s, "md5")    == 0) return HMAC_ALGO_MD5;
    if (strcmp(s, "sha1")   == 0) return HMAC_ALGO_SHA1;
    if (strcmp(s, "sha256") == 0) return HMAC_ALGO_SHA256;
    return -1;
}

static int hmac_digest_size(int algo) {
    switch (algo) {
        case HMAC_ALGO_MD5:    return 16;
        case HMAC_ALGO_SHA1:   return 20;
        case HMAC_ALGO_SHA256: return 32;
    }
    return 0;
}

/* ================================================================
 * Low-level HMAC helpers
 * ================================================================ */

/* Dispatch: init hash context */
static void hmac_hash_init(int algo, void* ctx) {
    switch (algo) {
        case HMAC_ALGO_MD5:    ms_md5_init   ((MD5_CTX*)ctx);    break;
        case HMAC_ALGO_SHA1:   ms_sha1_init  ((SHA1_CTX*)ctx);   break;
        case HMAC_ALGO_SHA256: ms_sha256_init((SHA256_CTX*)ctx); break;
    }
}

/* Dispatch: update hash context */
static void hmac_hash_update(int algo, void* ctx, const uint8_t* data, size_t len) {
    switch (algo) {
        case HMAC_ALGO_MD5:    ms_md5_update   ((MD5_CTX*)ctx,    data, len); break;
        case HMAC_ALGO_SHA1:   ms_sha1_update  ((SHA1_CTX*)ctx,   data, len); break;
        case HMAC_ALGO_SHA256: ms_sha256_update((SHA256_CTX*)ctx, data, len); break;
    }
}

/* Dispatch: finalize hash context */
static void hmac_hash_final(int algo, void* ctx, uint8_t* digest) {
    switch (algo) {
        case HMAC_ALGO_MD5:    ms_md5_final   ((MD5_CTX*)ctx,    digest); break;
        case HMAC_ALGO_SHA1:   ms_sha1_final  ((SHA1_CTX*)ctx,   digest); break;
        case HMAC_ALGO_SHA256: ms_sha256_final((SHA256_CTX*)ctx, digest); break;
    }
}

/* Dispatch: oneshot hash */
static void hmac_hash_oneshot(int algo, const uint8_t* data, size_t len, uint8_t* digest) {
    switch (algo) {
        case HMAC_ALGO_MD5:    ms_md5_oneshot   (data, len, digest); break;
        case HMAC_ALGO_SHA1:   ms_sha1_oneshot  (data, len, digest); break;
        case HMAC_ALGO_SHA256: ms_sha256_oneshot(data, len, digest); break;
    }
}

/* Build padded key K' (64 bytes).  Stores result in kprime[64]. */
static void hmac_derive_key(int algo, const uint8_t* key, size_t keylen,
                            uint8_t kprime[HMAC_BLOCK_SIZE]) {
    memset(kprime, 0, HMAC_BLOCK_SIZE);
    if (keylen > HMAC_BLOCK_SIZE) {
        /* K' = H(K) */
        hmac_hash_oneshot(algo, key, keylen, kprime);
    } else {
        memcpy(kprime, key, keylen);
    }
}

/* ctx storage: max of the three context sizes */
typedef union {
    MD5_CTX    md5;
    SHA1_CTX   sha1;
    SHA256_CTX sha256;
} HmacHashCtx;

/* Compute one-shot HMAC into digest[]. */
static void hmac_compute_raw(int algo,
                             const uint8_t* key, size_t keylen,
                             const uint8_t* data, size_t datalen,
                             uint8_t* out_digest) {
    uint8_t kprime[HMAC_BLOCK_SIZE];
    hmac_derive_key(algo, key, keylen, kprime);

    /* ipad = 0x36, opad = 0x5c */
    uint8_t ikey[HMAC_BLOCK_SIZE], okey[HMAC_BLOCK_SIZE];
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++) {
        ikey[i] = kprime[i] ^ 0x36u;
        okey[i] = kprime[i] ^ 0x5cu;
    }

    /* inner = H(ikey || data) */
    uint8_t inner[HMAC_MAX_DIGEST];
    HmacHashCtx ctx;
    hmac_hash_init(algo, &ctx);
    hmac_hash_update(algo, &ctx, ikey, HMAC_BLOCK_SIZE);
    hmac_hash_update(algo, &ctx, data, datalen);
    hmac_hash_final(algo, &ctx, inner);

    /* outer = H(okey || inner) */
    int dsize = hmac_digest_size(algo);
    hmac_hash_init(algo, &ctx);
    hmac_hash_update(algo, &ctx, okey, HMAC_BLOCK_SIZE);
    hmac_hash_update(algo, &ctx, inner, (size_t)dsize);
    hmac_hash_final(algo, &ctx, out_digest);
}

/* ================================================================
 * Value coercion helper (str|Buffer → raw bytes)
 * ================================================================ */

static bool hmac_coerce_bytes(MsValue v, const uint8_t** out, size_t* len) {
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
 * Build hex string from raw digest bytes
 * ================================================================ */

static MsValue hmac_bytes_to_hex(MsVM* vm, const uint8_t* d, int dsize) {
    MsObjBuffer* buf = ms_obj_buffer_from_bytes(vm, d, (size_t)dsize);
    MsValue bval = MS_OBJ_VAL(buf);
    MsValue out;
    if (!ms_objbuffer_invoke(vm, buf,
            ms_obj_string_copy(vm, "to_hex", 6),
            0, &bval, &out)) {
        return MS_NIL_VAL();
    }
    return out;
}

/* ================================================================
 * Streaming Hmac userdata
 * ================================================================ */

typedef struct {
    int     algo;
    /* ikey XOR'd already, ready for inner hash */
    uint8_t okey[HMAC_BLOCK_SIZE];
    HmacHashCtx inner_ctx;  /* running inner hash: H(ikey || data...) */
} MsHmacData;

static bool is_hmac(MsValue v) {
    return MS_IS_USERDATA(v) &&
           strcmp(MS_AS_USERDATA(v)->type_tag, "MsHmac") == 0;
}

/* hmac.new(algo, key) → Hmac */
static MsValue ms_hmac_new(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "hmac.new: expected string algo");
        return MS_NIL_VAL();
    }
    int algo = hmac_parse_algo(MS_AS_CSTRING(argv[0]));
    if (algo < 0) {
        ms_vm_runtime_error(vm, "hmac.new: unknown algo '%s'", MS_AS_CSTRING(argv[0]));
        return MS_NIL_VAL();
    }
    const uint8_t* key; size_t keylen;
    if (!hmac_coerce_bytes(argv[1], &key, &keylen)) {
        ms_vm_runtime_error(vm, "hmac.new: key must be str or Buffer");
        return MS_NIL_VAL();
    }

    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(MsHmacData),
                                            NULL, NULL, "MsHmac");
    MsHmacData* h = (MsHmacData*)ud->data;
    h->algo = algo;

    uint8_t kprime[HMAC_BLOCK_SIZE];
    hmac_derive_key(algo, key, keylen, kprime);

    uint8_t ikey[HMAC_BLOCK_SIZE];
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++) {
        ikey[i]   = kprime[i] ^ 0x36u;
        h->okey[i] = kprime[i] ^ 0x5cu;
    }

    hmac_hash_init(algo, &h->inner_ctx);
    hmac_hash_update(algo, &h->inner_ctx, ikey, HMAC_BLOCK_SIZE);
    return MS_OBJ_VAL(ud);
}

/* h.update(data) — appends data to inner hash */
static MsValue ms_hmac_update(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_hmac(argv[0])) {
        ms_vm_runtime_error(vm, "hmac.update: expected Hmac object");
        return MS_NIL_VAL();
    }
    MsHmacData* h = (MsHmacData*)MS_AS_USERDATA(argv[0])->data;
    const uint8_t* data; size_t len;
    if (!hmac_coerce_bytes(argv[1], &data, &len)) {
        ms_vm_runtime_error(vm, "hmac.update: data must be str or Buffer");
        return MS_NIL_VAL();
    }
    hmac_hash_update(h->algo, &h->inner_ctx, data, len);
    return MS_NIL_VAL();
}

/* Compute final HMAC from a copy of inner_ctx (non-destructive) */
static void hmac_finalize_copy(MsHmacData* h, uint8_t* out_digest) {
    /* copy inner context so we can call again */
    HmacHashCtx inner_copy = h->inner_ctx;
    uint8_t inner_digest[HMAC_MAX_DIGEST];
    hmac_hash_final(h->algo, &inner_copy, inner_digest);

    int dsize = hmac_digest_size(h->algo);
    HmacHashCtx outer_ctx;
    hmac_hash_init(h->algo, &outer_ctx);
    hmac_hash_update(h->algo, &outer_ctx, h->okey, HMAC_BLOCK_SIZE);
    hmac_hash_update(h->algo, &outer_ctx, inner_digest, (size_t)dsize);
    hmac_hash_final(h->algo, &outer_ctx, out_digest);
}

/* h.digest() → Buffer */
static MsValue ms_hmac_digest(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_hmac(argv[0])) {
        ms_vm_runtime_error(vm, "hmac.digest: expected Hmac object");
        return MS_NIL_VAL();
    }
    MsHmacData* h = (MsHmacData*)MS_AS_USERDATA(argv[0])->data;
    uint8_t digest[HMAC_MAX_DIGEST];
    hmac_finalize_copy(h, digest);
    int dsize = hmac_digest_size(h->algo);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, digest, (size_t)dsize));
}

/* h.hexdigest() → str */
static MsValue ms_hmac_hexdigest(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!is_hmac(argv[0])) {
        ms_vm_runtime_error(vm, "hmac.hexdigest: expected Hmac object");
        return MS_NIL_VAL();
    }
    MsHmacData* h = (MsHmacData*)MS_AS_USERDATA(argv[0])->data;
    uint8_t digest[HMAC_MAX_DIGEST];
    hmac_finalize_copy(h, digest);
    int dsize = hmac_digest_size(h->algo);
    return hmac_bytes_to_hex(vm, digest, dsize);
}

/* ================================================================
 * hmac.compute(algo, key, data) → str
 * ================================================================ */

static MsValue ms_hmac_compute(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "hmac.compute: algo must be a string");
        return MS_NIL_VAL();
    }
    int algo = hmac_parse_algo(MS_AS_CSTRING(argv[0]));
    if (algo < 0) {
        ms_vm_runtime_error(vm, "hmac.compute: unknown algo '%s'", MS_AS_CSTRING(argv[0]));
        return MS_NIL_VAL();
    }
    const uint8_t* key;  size_t keylen;
    const uint8_t* data; size_t datalen;
    if (!hmac_coerce_bytes(argv[1], &key, &keylen)) {
        ms_vm_runtime_error(vm, "hmac.compute: key must be str or Buffer");
        return MS_NIL_VAL();
    }
    if (!hmac_coerce_bytes(argv[2], &data, &datalen)) {
        ms_vm_runtime_error(vm, "hmac.compute: data must be str or Buffer");
        return MS_NIL_VAL();
    }
    uint8_t digest[HMAC_MAX_DIGEST];
    hmac_compute_raw(algo, key, keylen, data, datalen, digest);
    int dsize = hmac_digest_size(algo);
    return hmac_bytes_to_hex(vm, digest, dsize);
}

/* ================================================================
 * Registration
 * ================================================================ */

static const MsNativeDef ms_hmac_defs[] = {
    {"compute",   ms_hmac_compute,   3},
    {"new",       ms_hmac_new,       2},
    {"update",    ms_hmac_update,    2},
    {"digest",    ms_hmac_digest,    1},
    {"hexdigest", ms_hmac_hexdigest, 1},
    {NULL, NULL, 0}
};

void ms_module_hmac_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_hmac_defs);
}
