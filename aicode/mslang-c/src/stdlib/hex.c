/* src/stdlib/hex.c - STDLIB-26: hex module
 *
 * Public API:
 *   hex.encode(data)        → str   lowercase hex string
 *   hex.encode_upper(data)  → str   uppercase hex string
 *   hex.decode(s)           → buffer
 *   hex.is_valid(s)         → bool  even length, only 0-9 a-f A-F
 *   hex.byte_to_hex(n)      → str   single byte (0-255) → 2-char hex
 *   hex.hex_to_byte(s)      → int   2-char hex → byte value
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/stdlib/objbuffer.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Coerce str|buffer → raw bytes (does not copy). */
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

/* Return nibble value 0-15, or -1 for invalid. */
static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Core encode                                                         */
/* ------------------------------------------------------------------ */

static MsValue encode_impl(MsVM* vm, const uint8_t* src, size_t src_len,
                           bool upper) {
    size_t out_len = src_len * 2;
    char* buf = (char*)malloc(out_len + 1);
    if (!buf) {
        ms_vm_runtime_error(vm, "hex.encode: OOM");
        return MS_NIL_VAL();
    }
    const char* lo = "0123456789abcdef";
    const char* hi = "0123456789ABCDEF";
    const char* tbl = upper ? hi : lo;
    for (size_t i = 0; i < src_len; i++) {
        buf[i * 2]     = tbl[(src[i] >> 4) & 0xF];
        buf[i * 2 + 1] = tbl[src[i] & 0xF];
    }
    buf[out_len] = '\0';
    MsValue result = MS_OBJ_VAL(ms_obj_string_copy(vm, buf, (int)out_len));
    free(buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* Native functions                                                    */
/* ------------------------------------------------------------------ */

static MsValue hex_encode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "hex.encode: expected str or buffer");
        return MS_NIL_VAL();
    }
    return encode_impl(vm, data, len, false);
}

static MsValue hex_encode_upper(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* data; size_t len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "hex.encode_upper: expected str or buffer");
        return MS_NIL_VAL();
    }
    return encode_impl(vm, data, len, true);
}

static MsValue hex_decode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "hex.decode: expected str");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    const char* p  = s->data;
    int          n  = s->length;

    if (n % 2 != 0) {
        ms_vm_runtime_error(vm, "hex.decode: odd-length hex string");
        return MS_NIL_VAL();
    }

    /* Validate all chars first */
    for (int i = 0; i < n; i++) {
        if (hex_nibble(p[i]) < 0) {
            ms_vm_runtime_error(vm, "hex.decode: invalid character '%c' at index %d",
                                p[i], i);
            return MS_NIL_VAL();
        }
    }

    size_t out_len = (size_t)(n / 2);
    uint8_t* out = (uint8_t*)malloc(out_len == 0 ? 1 : out_len);
    if (!out) {
        ms_vm_runtime_error(vm, "hex.decode: OOM");
        return MS_NIL_VAL();
    }
    for (size_t i = 0; i < out_len; i++) {
        out[i] = (uint8_t)((hex_nibble(p[i * 2]) << 4) |
                            hex_nibble(p[i * 2 + 1]));
    }
    MsValue result = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, out, out_len));
    free(out);
    return result;
}

static MsValue hex_is_valid(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    if (!MS_IS_STRING(argv[0])) return MS_BOOL_VAL(false);
    MsObjString* s = MS_AS_STRING(argv[0]);
    if (s->length % 2 != 0) return MS_BOOL_VAL(false);
    for (int i = 0; i < s->length; i++) {
        if (hex_nibble(s->data[i]) < 0) return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

static MsValue hex_byte_to_hex(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ms_i64 n = MS_IS_INT(argv[0]) ? MS_AS_INT(argv[0])
                                   : (ms_i64)ms_as_double(argv[0]);
    if (n < 0 || n > 255) {
        ms_vm_runtime_error(vm, "hex.byte_to_hex: value %lld out of range 0-255",
                            (long long)n);
        return MS_NIL_VAL();
    }
    char buf[3];
    buf[0] = "0123456789abcdef"[(n >> 4) & 0xF];
    buf[1] = "0123456789abcdef"[n & 0xF];
    buf[2] = '\0';
    return MS_OBJ_VAL(ms_obj_string_copy(vm, buf, 2));
}

static MsValue hex_hex_to_byte(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "hex.hex_to_byte: expected str");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    if (s->length != 2) {
        ms_vm_runtime_error(vm, "hex.hex_to_byte: expected 2-character string");
        return MS_NIL_VAL();
    }
    int hi = hex_nibble(s->data[0]);
    int lo = hex_nibble(s->data[1]);
    if (hi < 0 || lo < 0) {
        ms_vm_runtime_error(vm, "hex.hex_to_byte: invalid hex character");
        return MS_NIL_VAL();
    }
    return MS_INT_VAL((ms_i64)((hi << 4) | lo));
}

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef hex_defs[] = {
    {"encode",       hex_encode,       1},
    {"encode_upper", hex_encode_upper, 1},
    {"decode",       hex_decode,       1},
    {"is_valid",     hex_is_valid,     1},
    {"byte_to_hex",  hex_byte_to_hex,  1},
    {"hex_to_byte",  hex_hex_to_byte,  1},
    {NULL, NULL, 0}
};

void ms_module_hex_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, hex_defs);
}
