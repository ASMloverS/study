/* src/stdlib/base64.c - STDLIB-25: base64 module (RFC 4648)
 *
 * Public API:
 *   base64.encode(data)          → str   standard Base64 with padding
 *   base64.decode(s)             → buffer
 *   base64.encode_url(data)      → str   URL-safe, no padding
 *   base64.decode_url(s)         → buffer
 *   base64.encode_raw(data)      → str   standard alphabet, no padding
 *   base64.decode_raw(s)         → buffer
 *   base64.is_valid(s)           → bool
 *   base64.encoded_len(n)        → int
 *   base64.decoded_len(s)        → int
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/stdlib/objbuffer.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Alphabets                                                            */
/* ------------------------------------------------------------------ */

static const char B64_STD[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char B64_URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/* Decode table: 0xFF = invalid */
static uint8_t b64_decode_table(char c, bool url_safe) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0' + 52);
    if (!url_safe) {
        if (c == '+') return 62;
        if (c == '/') return 63;
    } else {
        if (c == '-') return 62;
        if (c == '_') return 63;
    }
    return 0xFF;
}

/* ------------------------------------------------------------------ */
/* Helper: coerce str | buffer → raw bytes                            */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Core encode                                                         */
/* ------------------------------------------------------------------ */

static MsValue encode_impl(MsVM* vm, const uint8_t* src, size_t src_len,
                           const char* alpha, bool pad) {
    /* Output length: ceil(src_len / 3) * 4, or without padding: ... */
    size_t full_groups = src_len / 3;
    size_t rem         = src_len % 3;
    size_t out_len;
    if (pad) {
        out_len = (full_groups + (rem ? 1 : 0)) * 4;
    } else {
        out_len = full_groups * 4 + (rem == 1 ? 2 : rem == 2 ? 3 : 0);
    }

    char* buf = (char*)malloc(out_len + 1);
    if (!buf) {
        ms_vm_runtime_error(vm, "base64.encode: OOM");
        return MS_NIL_VAL();
    }

    size_t i = 0, o = 0;
    for (; i + 2 < src_len; i += 3) {
        uint32_t v = ((uint32_t)src[i] << 16) |
                     ((uint32_t)src[i+1] << 8) |
                     (uint32_t)src[i+2];
        buf[o++] = alpha[(v >> 18) & 0x3F];
        buf[o++] = alpha[(v >> 12) & 0x3F];
        buf[o++] = alpha[(v >>  6) & 0x3F];
        buf[o++] = alpha[(v      ) & 0x3F];
    }

    if (rem == 1) {
        uint32_t v = (uint32_t)src[i] << 16;
        buf[o++] = alpha[(v >> 18) & 0x3F];
        buf[o++] = alpha[(v >> 12) & 0x3F];
        if (pad) { buf[o++] = '='; buf[o++] = '='; }
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i+1] << 8);
        buf[o++] = alpha[(v >> 18) & 0x3F];
        buf[o++] = alpha[(v >> 12) & 0x3F];
        buf[o++] = alpha[(v >>  6) & 0x3F];
        if (pad) { buf[o++] = '='; }
    }

    buf[o] = '\0';
    MsValue result = MS_OBJ_VAL(ms_obj_string_copy(vm, buf, (int)out_len));
    free(buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* Core decode                                                         */
/* ------------------------------------------------------------------ */

static MsValue decode_impl(MsVM* vm, const char* src, size_t src_len,
                           bool url_safe) {
    if (src_len == 0) {
        return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, NULL, 0));
    }

    /* Strip trailing padding */
    size_t effective_len = src_len;
    while (effective_len > 0 && src[effective_len - 1] == '=') {
        effective_len--;
    }

    /* Validate all characters */
    for (size_t i = 0; i < effective_len; i++) {
        if (b64_decode_table(src[i], url_safe) == 0xFF) {
            ms_vm_runtime_error(vm, "base64.decode: invalid character '%c' at index %d",
                                src[i], (int)i);
            return MS_NIL_VAL();
        }
    }

    /* Compute output length */
    size_t out_len = (effective_len * 3) / 4;
    uint8_t* out = (uint8_t*)malloc(out_len + 1);
    if (!out) {
        ms_vm_runtime_error(vm, "base64.decode: OOM");
        return MS_NIL_VAL();
    }

    size_t i = 0, o = 0;
    while (i + 3 < effective_len) {
        uint32_t v = ((uint32_t)b64_decode_table(src[i],   url_safe) << 18) |
                     ((uint32_t)b64_decode_table(src[i+1], url_safe) << 12) |
                     ((uint32_t)b64_decode_table(src[i+2], url_safe) <<  6) |
                     ((uint32_t)b64_decode_table(src[i+3], url_safe));
        out[o++] = (uint8_t)((v >> 16) & 0xFF);
        out[o++] = (uint8_t)((v >>  8) & 0xFF);
        out[o++] = (uint8_t)((v      ) & 0xFF);
        i += 4;
    }

    size_t rem = effective_len - i;
    if (rem == 2) {
        uint32_t v = ((uint32_t)b64_decode_table(src[i],   url_safe) << 18) |
                     ((uint32_t)b64_decode_table(src[i+1], url_safe) << 12);
        out[o++] = (uint8_t)((v >> 16) & 0xFF);
    } else if (rem == 3) {
        uint32_t v = ((uint32_t)b64_decode_table(src[i],   url_safe) << 18) |
                     ((uint32_t)b64_decode_table(src[i+1], url_safe) << 12) |
                     ((uint32_t)b64_decode_table(src[i+2], url_safe) <<  6);
        out[o++] = (uint8_t)((v >> 16) & 0xFF);
        out[o++] = (uint8_t)((v >>  8) & 0xFF);
    }

    MsValue result = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, out, o));
    free(out);
    return result;
}

/* ------------------------------------------------------------------ */
/* Native functions                                                    */
/* ------------------------------------------------------------------ */

static MsValue base64_encode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "base64.encode: expected str or buffer");
        return MS_NIL_VAL();
    }
    return encode_impl(vm, data, len, B64_STD, true);
}

static MsValue base64_decode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "base64.decode: expected str");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return decode_impl(vm, s->data, (size_t)s->length, false);
}

static MsValue base64_encode_url(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "base64.encode_url: expected str or buffer");
        return MS_NIL_VAL();
    }
    return encode_impl(vm, data, len, B64_URL, false);
}

static MsValue base64_decode_url(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "base64.decode_url: expected str");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return decode_impl(vm, s->data, (size_t)s->length, true);
}

static MsValue base64_encode_raw(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "base64.encode_raw: expected str or buffer");
        return MS_NIL_VAL();
    }
    return encode_impl(vm, data, len, B64_STD, false);
}

static MsValue base64_decode_raw(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "base64.decode_raw: expected str");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return decode_impl(vm, s->data, (size_t)s->length, false);
}

static MsValue base64_is_valid(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    if (!MS_IS_STRING(argv[0])) return MS_BOOL_VAL(false);
    MsObjString* s = MS_AS_STRING(argv[0]);
    const char* data = s->data;
    size_t len = (size_t)s->length;

    /* Strip trailing '=' */
    size_t eff = len;
    while (eff > 0 && data[eff - 1] == '=') eff--;

    for (size_t i = 0; i < eff; i++) {
        if (b64_decode_table(data[i], false) == 0xFF) {
            return MS_BOOL_VAL(false);
        }
    }
    return MS_BOOL_VAL(true);
}

static MsValue base64_encoded_len(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    ms_i64 n = MS_IS_INT(argv[0]) ? MS_AS_INT(argv[0])
                                   : (ms_i64)ms_as_double(argv[0]);
    if (n <= 0) return MS_INT_VAL(0);
    /* ceil(n/3)*4 */
    ms_i64 result = ((n + 2) / 3) * 4;
    return MS_INT_VAL(result);
}

static MsValue base64_decoded_len(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    if (!MS_IS_STRING(argv[0])) return MS_INT_VAL(0);
    MsObjString* s = MS_AS_STRING(argv[0]);
    size_t len = (size_t)s->length;
    if (len == 0) return MS_INT_VAL(0);
    /* max decoded bytes = len/4*3 (rough upper bound) */
    ms_i64 result = (ms_i64)((len / 4) * 3 + (len % 4 > 1 ? len % 4 - 1 : 0));
    return MS_INT_VAL(result);
}

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef base64_defs[] = {
    {"encode",      base64_encode,      1},
    {"decode",      base64_decode,      1},
    {"encode_url",  base64_encode_url,  1},
    {"decode_url",  base64_decode_url,  1},
    {"encode_raw",  base64_encode_raw,  1},
    {"decode_raw",  base64_decode_raw,  1},
    {"is_valid",    base64_is_valid,    1},
    {"encoded_len", base64_encoded_len, 1},
    {"decoded_len", base64_decoded_len, 1},
    {NULL, NULL, 0}
};

void ms_module_base64_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, base64_defs);
}
