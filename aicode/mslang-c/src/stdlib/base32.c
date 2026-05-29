/* src/stdlib/base32.c - STDLIB-34: base32 module (RFC 4648)
 *
 * Public API:
 *   base32.encode(data)      → str   standard Base32 (A-Z2-7) with padding
 *   base32.decode(s)         → buffer
 *   base32.encode_hex(data)  → str   extended hex alphabet (0-9A-V) with padding
 *   base32.decode_hex(s)     → buffer
 *   base32.encode_raw(data)  → str   standard alphabet, no padding
 *   base32.is_valid(s)       → bool
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

/* RFC 4648 §6 standard alphabet */
static const char B32_STD[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
/* RFC 4648 §7 extended-hex alphabet */
static const char B32_HEX[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

/* Return 5-bit value 0-31 for a character, or 0xFF for invalid. */
static uint8_t b32_decode_char(char c, bool hex_alpha) {
    if (!hex_alpha) {
        /* standard: A-Z = 0-25, 2-7 = 26-31 */
        if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
        if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a');
        if (c >= '2' && c <= '7') return (uint8_t)(c - '2' + 26);
    } else {
        /* hex: 0-9 = 0-9, A-V = 10-31 */
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'A' && c <= 'V') return (uint8_t)(c - 'A' + 10);
        if (c >= 'a' && c <= 'v') return (uint8_t)(c - 'a' + 10);
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

/* Base32 encodes 5 bytes → 8 chars. */
static MsValue encode_impl(MsVM* vm, const uint8_t* src, size_t src_len,
                           const char* alpha, bool pad) {
    size_t full_groups = src_len / 5;
    size_t rem         = src_len % 5;

    /* Output chars per remainder:
     *   0 → 0, 1 → 2, 2 → 4, 3 → 5, 4 → 7
     * With padding always round up to multiple of 8. */
    static const size_t rem_chars[] = {0, 2, 4, 5, 7};
    size_t out_len;
    if (pad) {
        out_len = (full_groups + (rem ? 1 : 0)) * 8;
    } else {
        out_len = full_groups * 8 + rem_chars[rem];
    }

    char* buf = (char*)malloc(out_len + 1);
    if (!buf) {
        ms_vm_runtime_error(vm, "base32.encode: OOM");
        return MS_NIL_VAL();
    }

    size_t i = 0, o = 0;
    /* Full 5-byte groups */
    for (; i + 4 < src_len; i += 5) {
        uint64_t v = ((uint64_t)src[i]   << 32) |
                     ((uint64_t)src[i+1] << 24) |
                     ((uint64_t)src[i+2] << 16) |
                     ((uint64_t)src[i+3] <<  8) |
                     (uint64_t)src[i+4];
        buf[o++] = alpha[(v >> 35) & 0x1F];
        buf[o++] = alpha[(v >> 30) & 0x1F];
        buf[o++] = alpha[(v >> 25) & 0x1F];
        buf[o++] = alpha[(v >> 20) & 0x1F];
        buf[o++] = alpha[(v >> 15) & 0x1F];
        buf[o++] = alpha[(v >> 10) & 0x1F];
        buf[o++] = alpha[(v >>  5) & 0x1F];
        buf[o++] = alpha[(v      ) & 0x1F];
    }

    /* Remaining bytes */
    if (rem >= 1) {
        uint64_t v = (uint64_t)src[i] << 32;
        if (rem >= 2) v |= (uint64_t)src[i+1] << 24;
        if (rem >= 3) v |= (uint64_t)src[i+2] << 16;
        if (rem >= 4) v |= (uint64_t)src[i+3] <<  8;

        buf[o++] = alpha[(v >> 35) & 0x1F];
        buf[o++] = alpha[(v >> 30) & 0x1F];
        if (rem >= 2) {
            buf[o++] = alpha[(v >> 25) & 0x1F];
            buf[o++] = alpha[(v >> 20) & 0x1F];
        }
        if (rem >= 3) buf[o++] = alpha[(v >> 15) & 0x1F];
        if (rem >= 4) {
            buf[o++] = alpha[(v >> 10) & 0x1F];
            buf[o++] = alpha[(v >>  5) & 0x1F];
        }

        if (pad) {
            /* Pad to next multiple of 8 */
            static const size_t pad_counts[] = {0, 6, 4, 3, 1};
            size_t np = pad_counts[rem];
            for (size_t p = 0; p < np; p++) buf[o++] = '=';
        }
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
                           bool hex_alpha) {
    if (src_len == 0) {
        return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, NULL, 0));
    }

    /* Strip trailing '=' padding */
    size_t eff = src_len;
    while (eff > 0 && src[eff - 1] == '=') eff--;

    /* Validate all chars */
    for (size_t i = 0; i < eff; i++) {
        if (b32_decode_char(src[i], hex_alpha) == 0xFF) {
            ms_vm_runtime_error(vm,
                "base32.decode: invalid character '%c' at index %d",
                src[i], (int)i);
            return MS_NIL_VAL();
        }
    }

    /* Output bytes: floor(eff * 5 / 8) */
    size_t out_len = (eff * 5) / 8;
    uint8_t* out = (uint8_t*)malloc(out_len == 0 ? 1 : out_len);
    if (!out) {
        ms_vm_runtime_error(vm, "base32.decode: OOM");
        return MS_NIL_VAL();
    }

    size_t i = 0, o = 0;
    /* Decode full 8-char groups */
    while (i + 7 < eff) {
        uint64_t v = ((uint64_t)b32_decode_char(src[i  ], hex_alpha) << 35) |
                     ((uint64_t)b32_decode_char(src[i+1], hex_alpha) << 30) |
                     ((uint64_t)b32_decode_char(src[i+2], hex_alpha) << 25) |
                     ((uint64_t)b32_decode_char(src[i+3], hex_alpha) << 20) |
                     ((uint64_t)b32_decode_char(src[i+4], hex_alpha) << 15) |
                     ((uint64_t)b32_decode_char(src[i+5], hex_alpha) << 10) |
                     ((uint64_t)b32_decode_char(src[i+6], hex_alpha) <<  5) |
                     ((uint64_t)b32_decode_char(src[i+7], hex_alpha));
        out[o++] = (uint8_t)((v >> 32) & 0xFF);
        out[o++] = (uint8_t)((v >> 24) & 0xFF);
        out[o++] = (uint8_t)((v >> 16) & 0xFF);
        out[o++] = (uint8_t)((v >>  8) & 0xFF);
        out[o++] = (uint8_t)((v      ) & 0xFF);
        i += 8;
    }

    /* Remaining chars */
    size_t rem = eff - i;
    if (rem >= 2) {
        uint64_t v = ((uint64_t)b32_decode_char(src[i  ], hex_alpha) << 35) |
                     ((uint64_t)b32_decode_char(src[i+1], hex_alpha) << 30);
        if (rem >= 3) v |= (uint64_t)b32_decode_char(src[i+2], hex_alpha) << 25;
        if (rem >= 4) v |= (uint64_t)b32_decode_char(src[i+3], hex_alpha) << 20;
        if (rem >= 5) v |= (uint64_t)b32_decode_char(src[i+4], hex_alpha) << 15;
        if (rem >= 6) v |= (uint64_t)b32_decode_char(src[i+5], hex_alpha) << 10;
        if (rem >= 7) v |= (uint64_t)b32_decode_char(src[i+6], hex_alpha) <<  5;

        out[o++] = (uint8_t)((v >> 32) & 0xFF);
        if (rem >= 4) out[o++] = (uint8_t)((v >> 24) & 0xFF);
        if (rem >= 5) out[o++] = (uint8_t)((v >> 16) & 0xFF);
        if (rem >= 7) out[o++] = (uint8_t)((v >>  8) & 0xFF);
    }

    MsValue result = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, out, o));
    free(out);
    return result;
}

/* ------------------------------------------------------------------ */
/* Native functions                                                    */
/* ------------------------------------------------------------------ */

static MsValue base32_encode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "base32.encode: expected str or buffer");
        return MS_NIL_VAL();
    }
    return encode_impl(vm, data, len, B32_STD, true);
}

static MsValue base32_decode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "base32.decode: expected str");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return decode_impl(vm, s->data, (size_t)s->length, false);
}

static MsValue base32_encode_hex(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "base32.encode_hex: expected str or buffer");
        return MS_NIL_VAL();
    }
    return encode_impl(vm, data, len, B32_HEX, true);
}

static MsValue base32_decode_hex(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "base32.decode_hex: expected str");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return decode_impl(vm, s->data, (size_t)s->length, true);
}

static MsValue base32_encode_raw(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "base32.encode_raw: expected str or buffer");
        return MS_NIL_VAL();
    }
    return encode_impl(vm, data, len, B32_STD, false);
}

static MsValue base32_is_valid(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    if (!MS_IS_STRING(argv[0])) return MS_BOOL_VAL(false);
    MsObjString* s = MS_AS_STRING(argv[0]);
    const char* data = s->data;
    size_t len = (size_t)s->length;

    /* Strip trailing '=' */
    size_t eff = len;
    while (eff > 0 && data[eff - 1] == '=') eff--;

    for (size_t i = 0; i < eff; i++) {
        if (b32_decode_char(data[i], false) == 0xFF) {
            return MS_BOOL_VAL(false);
        }
    }
    return MS_BOOL_VAL(true);
}

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef base32_defs[] = {
    {"encode",      base32_encode,      1},
    {"decode",      base32_decode,      1},
    {"encode_hex",  base32_encode_hex,  1},
    {"decode_hex",  base32_decode_hex,  1},
    {"encode_raw",  base32_encode_raw,  1},
    {"is_valid",    base32_is_valid,    1},
    {NULL, NULL, 0}
};

void ms_module_base32_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, base32_defs);
}
