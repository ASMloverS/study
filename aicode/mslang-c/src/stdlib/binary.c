/* src/stdlib/binary.c - STDLIB-32: binary module
 *
 * Binary pack/unpack (big/little endian) and varint (LEB128) codec.
 *
 * Public API:
 *   binary.pack(fmt, ...)               → buffer
 *   binary.unpack(fmt, buf, offset=0)   → list
 *   binary.pack_into(buf, off, fmt, ...) → nil
 *   binary.calc_size(fmt)               → int
 *   binary.varint_encode(n)             → buffer
 *   binary.varint_decode(buf, offset=0) → [int, int]
 *   binary.svarint_encode(n)            → buffer
 *   binary.svarint_decode(buf, offset=0)→ [int, int]
 *   binary.read_u8(buf, offset)         → int
 *   binary.read_u16_le(buf, offset)     → int
 *   binary.read_u16_be(buf, offset)     → int
 *   binary.read_u32_le(buf, offset)     → int
 *   binary.read_u32_be(buf, offset)     → int
 *   binary.read_u64_le(buf, offset)     → int
 *   binary.read_u64_be(buf, offset)     → int
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
/* Endian helpers                                                      */
/* ------------------------------------------------------------------ */

/* Determine platform byte order at runtime (C11 compatible). */
static bool is_little_endian(void) {
    uint16_t v = 1;
    return *(uint8_t*)&v == 1;
}

/* ---- unaligned write helpers ---- */
static void write_u8(uint8_t* p, uint8_t v)   { p[0] = v; }
static void write_u16_be(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void write_u16_le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void write_u32_be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t)v;
}
static void write_u32_le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >>  8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void write_u64_be(uint8_t* p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >>  8); p[7] = (uint8_t)v;
}
static void write_u64_le(uint8_t* p, uint64_t v) {
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >>  8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32); p[5] = (uint8_t)(v >> 40);
    p[6] = (uint8_t)(v >> 48); p[7] = (uint8_t)(v >> 56);
}

/* ---- unaligned read helpers ---- */
static uint16_t read_u16_be(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static uint16_t read_u16_le(const uint8_t* p) {
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}
static uint32_t read_u32_le(const uint8_t* p) {
    return  (uint32_t)p[0]        | ((uint32_t)p[1] <<  8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t read_u64_be(const uint8_t* p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] <<  8) |  (uint64_t)p[7];
}
static uint64_t read_u64_le(const uint8_t* p) {
    return  (uint64_t)p[0]        | ((uint64_t)p[1] <<  8) |
           ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

/* ------------------------------------------------------------------ */
/* Format parsing                                                      */
/* ------------------------------------------------------------------ */

/* Endian mode encoded in format prefix. */
typedef enum { END_BIG, END_LITTLE, END_NATIVE } Endian;

/* Parse format string prefix into endian + pointer past prefix.
 * Returns false if prefix is invalid. */
static bool parse_endian(const char* fmt, Endian* end, const char** rest) {
    if (*fmt == '>') { *end = END_BIG;    *rest = fmt + 1; return true; }
    if (*fmt == '<') { *end = END_LITTLE; *rest = fmt + 1; return true; }
    if (*fmt == '=') { *end = END_NATIVE; *rest = fmt + 1; return true; }
    /* default big-endian when no prefix provided */
    *end  = END_BIG;
    *rest = fmt;
    return true;
}

/* Resolve END_NATIVE to actual endian. */
static Endian resolve_endian(Endian e) {
    if (e == END_NATIVE) return is_little_endian() ? END_LITTLE : END_BIG;
    return e;
}

/* Compute size (in bytes) of a format specifier character (no repeat count). */
static int fmt_char_size(char c) {
    switch (c) {
        case 'b': case 'B': return 1;
        case 'h': case 'H': return 2;
        case 'i': case 'I': return 4;
        case 'q': case 'Q': return 8;
        case 'f':           return 4;
        case 'd':           return 8;
        default:            return -1; /* unknown or 's' without count */
    }
}

/* Compute total byte size of a format string (past the endian prefix).
 * Returns -1 on error. */
static int calc_fmt_size(const char* fmt) {
    int total = 0;
    while (*fmt) {
        /* Optional repeat count */
        int count = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            count = count * 10 + (*fmt - '0');
            fmt++;
        }
        if (!*fmt) return -1;
        char c = *fmt++;
        if (c == 's') {
            if (count <= 0) return -1; /* 's' requires count */
            total += count;
        } else {
            if (count <= 0) count = 1;
            int sz = fmt_char_size(c);
            if (sz < 0) return -1;
            total += sz * count;
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* coerce: get raw bytes from str | buffer                            */
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
/* Pack / unpack implementation                                        */
/* ------------------------------------------------------------------ */

/* Write a single format item into dst[offset].
 * Returns bytes written, or -1 on type error. */
static int pack_one(MsVM* vm, char c, Endian end,
                    MsValue val, uint8_t* dst) {
    uint64_t u64; uint32_t u32; uint16_t u16;
    uint32_t fbit; uint64_t dbit;
    float  fv; double dv;
    int64_t sval;

    switch (c) {
        case 'b':
            sval = MS_IS_INT(val) ? MS_AS_INT(val) : (int64_t)ms_as_double(val);
            write_u8(dst, (uint8_t)(int8_t)sval);
            return 1;
        case 'B':
            u64 = MS_IS_INT(val) ? (uint64_t)MS_AS_INT(val) : (uint64_t)ms_as_double(val);
            write_u8(dst, (uint8_t)u64);
            return 1;
        case 'h':
            sval = MS_IS_INT(val) ? MS_AS_INT(val) : (int64_t)ms_as_double(val);
            u16 = (uint16_t)(int16_t)sval;
            if (end == END_BIG) write_u16_be(dst, u16); else write_u16_le(dst, u16);
            return 2;
        case 'H':
            u64 = MS_IS_INT(val) ? (uint64_t)MS_AS_INT(val) : (uint64_t)ms_as_double(val);
            u16 = (uint16_t)u64;
            if (end == END_BIG) write_u16_be(dst, u16); else write_u16_le(dst, u16);
            return 2;
        case 'i':
            sval = MS_IS_INT(val) ? MS_AS_INT(val) : (int64_t)ms_as_double(val);
            u32 = (uint32_t)(int32_t)sval;
            if (end == END_BIG) write_u32_be(dst, u32); else write_u32_le(dst, u32);
            return 4;
        case 'I':
            u64 = MS_IS_INT(val) ? (uint64_t)MS_AS_INT(val) : (uint64_t)ms_as_double(val);
            u32 = (uint32_t)u64;
            if (end == END_BIG) write_u32_be(dst, u32); else write_u32_le(dst, u32);
            return 4;
        case 'q':
            sval = MS_IS_INT(val) ? MS_AS_INT(val) : (int64_t)ms_as_double(val);
            u64 = (uint64_t)sval;
            if (end == END_BIG) write_u64_be(dst, u64); else write_u64_le(dst, u64);
            return 8;
        case 'Q':
            u64 = MS_IS_INT(val) ? (uint64_t)MS_AS_INT(val) : (uint64_t)ms_as_double(val);
            if (end == END_BIG) write_u64_be(dst, u64); else write_u64_le(dst, u64);
            return 8;
        case 'f':
            fv = (float)(MS_IS_INT(val) ? (double)MS_AS_INT(val) : ms_as_double(val));
            memcpy(&fbit, &fv, 4);
            if (end == END_BIG) write_u32_be(dst, fbit); else write_u32_le(dst, fbit);
            return 4;
        case 'd':
            dv = MS_IS_INT(val) ? (double)MS_AS_INT(val) : ms_as_double(val);
            memcpy(&dbit, &dv, 8);
            if (end == END_BIG) write_u64_be(dst, dbit); else write_u64_le(dst, dbit);
            return 8;
        default:
            ms_vm_runtime_error(vm, "binary.pack: unknown format char '%c'", c);
            return -1;
    }
}

/* Read a single format item from src[offset].
 * Returns the MsValue and advances *offset, or returns NIL on bounds error. */
static MsValue unpack_one(MsVM* vm, char c, Endian end,
                          const uint8_t* src, size_t src_len,
                          size_t* offset) {
    size_t off = *offset;
    uint64_t u64; uint32_t u32; uint16_t u16;
    uint32_t fbit; uint64_t dbit;
    float fv; double dv;

#define NEED(n) do { \
    if (off + (n) > src_len) { \
        ms_vm_runtime_error(vm, "binary.unpack: buffer too short at offset %d", (int)off); \
        return MS_NIL_VAL(); \
    } \
} while(0)

    switch (c) {
        case 'b':
            NEED(1);
            *offset = off + 1;
            return MS_INT_VAL((int64_t)(int8_t)src[off]);
        case 'B':
            NEED(1);
            *offset = off + 1;
            return MS_INT_VAL((int64_t)src[off]);
        case 'h':
            NEED(2);
            u16 = end == END_BIG ? read_u16_be(src+off) : read_u16_le(src+off);
            *offset = off + 2;
            return MS_INT_VAL((int64_t)(int16_t)u16);
        case 'H':
            NEED(2);
            u16 = end == END_BIG ? read_u16_be(src+off) : read_u16_le(src+off);
            *offset = off + 2;
            return MS_INT_VAL((int64_t)u16);
        case 'i':
            NEED(4);
            u32 = end == END_BIG ? read_u32_be(src+off) : read_u32_le(src+off);
            *offset = off + 4;
            return MS_INT_VAL((int64_t)(int32_t)u32);
        case 'I':
            NEED(4);
            u32 = end == END_BIG ? read_u32_be(src+off) : read_u32_le(src+off);
            *offset = off + 4;
            return MS_INT_VAL((int64_t)(uint32_t)u32);
        case 'q':
            NEED(8);
            u64 = end == END_BIG ? read_u64_be(src+off) : read_u64_le(src+off);
            *offset = off + 8;
            return MS_INT_VAL((int64_t)u64);
        case 'Q':
            NEED(8);
            u64 = end == END_BIG ? read_u64_be(src+off) : read_u64_le(src+off);
            *offset = off + 8;
            /* Unsigned 64 — use INT (may wrap, but consistent) */
            return MS_INT_VAL((int64_t)u64);
        case 'f':
            NEED(4);
            fbit = end == END_BIG ? read_u32_be(src+off) : read_u32_le(src+off);
            memcpy(&fv, &fbit, 4);
            *offset = off + 4;
            return MS_NUMBER_VAL((double)fv);
        case 'd':
            NEED(8);
            dbit = end == END_BIG ? read_u64_be(src+off) : read_u64_le(src+off);
            memcpy(&dv, &dbit, 8);
            *offset = off + 8;
            return MS_NUMBER_VAL(dv);
        default:
            ms_vm_runtime_error(vm, "binary.unpack: unknown format char '%c'", c);
            return MS_NIL_VAL();
    }
#undef NEED
}

/* ------------------------------------------------------------------ */
/* binary.calc_size(fmt)                                              */
/* ------------------------------------------------------------------ */

static MsValue binary_calc_size(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "binary.calc_size: expected string format");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    Endian end; const char* rest;
    parse_endian(s->data, &end, &rest);
    int sz = calc_fmt_size(rest);
    if (sz < 0) {
        ms_vm_runtime_error(vm, "binary.calc_size: invalid format string");
        return MS_NIL_VAL();
    }
    return MS_INT_VAL((int64_t)sz);
}

/* ------------------------------------------------------------------ */
/* binary.pack(fmt, ...)                                              */
/* ------------------------------------------------------------------ */

static MsValue binary_pack(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "binary.pack: first arg must be format string");
        return MS_NIL_VAL();
    }
    MsObjString* fmtstr = MS_AS_STRING(argv[0]);
    Endian end_raw; const char* fmt;
    parse_endian(fmtstr->data, &end_raw, &fmt);
    Endian end = resolve_endian(end_raw);

    int sz = calc_fmt_size(fmt);
    if (sz < 0) {
        ms_vm_runtime_error(vm, "binary.pack: invalid format string");
        return MS_NIL_VAL();
    }

    uint8_t* buf = (uint8_t*)malloc((size_t)sz == 0 ? 1 : (size_t)sz);
    if (!buf) { ms_vm_runtime_error(vm, "binary.pack: OOM"); return MS_NIL_VAL(); }

    int arg_idx = 1; /* argv[0] is fmt */
    size_t off = 0;
    const char* p = fmt;
    while (*p) {
        int count = 0;
        while (*p >= '0' && *p <= '9') { count = count * 10 + (*p - '0'); p++; }
        if (!*p) break;
        char c = *p++;
        if (c == 's') {
            /* raw bytes from str/buffer argument */
            if (arg_idx >= argc) {
                ms_vm_runtime_error(vm, "binary.pack: not enough arguments");
                free(buf); return MS_NIL_VAL();
            }
            const uint8_t* src; size_t src_len;
            if (!coerce_bytes(argv[arg_idx++], &src, &src_len)) {
                ms_vm_runtime_error(vm, "binary.pack: 's' expects str or buffer");
                free(buf); return MS_NIL_VAL();
            }
            size_t copy = (size_t)count < src_len ? (size_t)count : src_len;
            memcpy(buf + off, src, copy);
            if ((size_t)count > copy) memset(buf + off + copy, 0, (size_t)count - copy);
            off += (size_t)count;
        } else {
            if (count <= 0) count = 1;
            for (int i = 0; i < count; i++) {
                if (arg_idx >= argc) {
                    ms_vm_runtime_error(vm, "binary.pack: not enough arguments");
                    free(buf); return MS_NIL_VAL();
                }
                int written = pack_one(vm, c, end, argv[arg_idx++], buf + off);
                if (written < 0) { free(buf); return MS_NIL_VAL(); }
                off += (size_t)written;
            }
        }
    }

    MsValue result = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, buf, (size_t)sz));
    free(buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* binary.pack_into(buf, offset, fmt, ...)                            */
/* ------------------------------------------------------------------ */

static MsValue binary_pack_into(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 3) {
        ms_vm_runtime_error(vm, "binary.pack_into: expected (buf, offset, fmt, ...)");
        return MS_NIL_VAL();
    }
    if (!MS_IS_BUFFER(argv[0])) {
        ms_vm_runtime_error(vm, "binary.pack_into: first arg must be buffer");
        return MS_NIL_VAL();
    }
    MsObjBuffer* dst_buf = MS_AS_BUFFER(argv[0]);
    int64_t base_off = MS_IS_INT(argv[1]) ? MS_AS_INT(argv[1]) : (int64_t)ms_as_double(argv[1]);
    if (base_off < 0 || (size_t)base_off > dst_buf->len) {
        ms_vm_runtime_error(vm, "binary.pack_into: offset out of range");
        return MS_NIL_VAL();
    }
    if (!MS_IS_STRING(argv[2])) {
        ms_vm_runtime_error(vm, "binary.pack_into: third arg must be format string");
        return MS_NIL_VAL();
    }
    MsObjString* fmtstr = MS_AS_STRING(argv[2]);
    Endian end_raw; const char* fmt;
    parse_endian(fmtstr->data, &end_raw, &fmt);
    Endian end = resolve_endian(end_raw);

    int sz = calc_fmt_size(fmt);
    if (sz < 0) { ms_vm_runtime_error(vm, "binary.pack_into: invalid format"); return MS_NIL_VAL(); }
    if ((size_t)base_off + (size_t)sz > dst_buf->len) {
        ms_vm_runtime_error(vm, "binary.pack_into: data would exceed buffer bounds");
        return MS_NIL_VAL();
    }

    int arg_idx = 3;
    size_t off = (size_t)base_off;
    const char* p = fmt;
    while (*p) {
        int count = 0;
        while (*p >= '0' && *p <= '9') { count = count * 10 + (*p - '0'); p++; }
        if (!*p) break;
        char c = *p++;
        if (c == 's') {
            if (arg_idx >= argc) {
                ms_vm_runtime_error(vm, "binary.pack_into: not enough arguments");
                return MS_NIL_VAL();
            }
            const uint8_t* src; size_t src_len;
            if (!coerce_bytes(argv[arg_idx++], &src, &src_len)) {
                ms_vm_runtime_error(vm, "binary.pack_into: 's' expects str or buffer");
                return MS_NIL_VAL();
            }
            size_t copy = (size_t)count < src_len ? (size_t)count : src_len;
            memcpy(dst_buf->data + off, src, copy);
            if ((size_t)count > copy) memset(dst_buf->data + off + copy, 0, (size_t)count - copy);
            off += (size_t)count;
        } else {
            if (count <= 0) count = 1;
            for (int i = 0; i < count; i++) {
                if (arg_idx >= argc) {
                    ms_vm_runtime_error(vm, "binary.pack_into: not enough arguments");
                    return MS_NIL_VAL();
                }
                int written = pack_one(vm, c, end, argv[arg_idx++], dst_buf->data + off);
                if (written < 0) return MS_NIL_VAL();
                off += (size_t)written;
            }
        }
    }
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* binary.unpack(fmt, buf, offset=0)                                  */
/* ------------------------------------------------------------------ */

static MsValue binary_unpack(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "binary.unpack: expected (fmt, buf [, offset])");
        return MS_NIL_VAL();
    }
    const uint8_t* src; size_t src_len;
    if (!coerce_bytes(argv[1], &src, &src_len)) {
        ms_vm_runtime_error(vm, "binary.unpack: second arg must be str or buffer");
        return MS_NIL_VAL();
    }
    size_t off = 0;
    if (argc >= 3) {
        int64_t o = MS_IS_INT(argv[2]) ? MS_AS_INT(argv[2]) : (int64_t)ms_as_double(argv[2]);
        if (o < 0 || (size_t)o > src_len) {
            ms_vm_runtime_error(vm, "binary.unpack: offset out of range");
            return MS_NIL_VAL();
        }
        off = (size_t)o;
    }

    MsObjString* fmtstr = MS_AS_STRING(argv[0]);
    Endian end_raw; const char* fmt;
    parse_endian(fmtstr->data, &end_raw, &fmt);
    Endian end = resolve_endian(end_raw);

    /* Build result list */
    MsObjList* list = ms_obj_list_new(vm);
    const char* p = fmt;
    while (*p) {
        int count = 0;
        while (*p >= '0' && *p <= '9') { count = count * 10 + (*p - '0'); p++; }
        if (!*p) break;
        char c = *p++;
        if (c == 's') {
            /* return raw bytes as buffer */
            if (off + (size_t)count > src_len) {
                ms_vm_runtime_error(vm, "binary.unpack: buffer too short for 's'");
                return MS_NIL_VAL();
            }
            MsValue bv = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, src + off, (size_t)count));
            ms_value_array_push(&list->items, bv);
            off += (size_t)count;
        } else {
            if (count <= 0) count = 1;
            for (int i = 0; i < count; i++) {
                size_t prev_off = off;
                MsValue v = unpack_one(vm, c, end, src, src_len, &off);
                if (MS_IS_NIL(v) && off == prev_off) return MS_NIL_VAL(); /* error */
                ms_value_array_push(&list->items, v);
            }
        }
    }
    return MS_OBJ_VAL(list);
}

/* ------------------------------------------------------------------ */
/* Varint (LEB128)                                                    */
/* ------------------------------------------------------------------ */

/* Unsigned LEB128 encode */
static MsValue binary_varint_encode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    uint64_t n = MS_IS_INT(argv[0]) ? (uint64_t)MS_AS_INT(argv[0])
                                     : (uint64_t)ms_as_double(argv[0]);
    uint8_t tmp[10]; int len = 0;
    do {
        uint8_t byte = (uint8_t)(n & 0x7F);
        n >>= 7;
        if (n) byte |= 0x80;
        tmp[len++] = byte;
    } while (n);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, tmp, (size_t)len));
}

/* Unsigned LEB128 decode → [value, bytes_consumed] */
static MsValue binary_varint_decode(MsVM* vm, int argc, MsValue* argv) {
    const uint8_t* src; size_t src_len;
    if (!coerce_bytes(argv[0], &src, &src_len)) {
        ms_vm_runtime_error(vm, "binary.varint_decode: expected buffer or str");
        return MS_NIL_VAL();
    }
    size_t off = 0;
    if (argc >= 2) {
        int64_t o = MS_IS_INT(argv[1]) ? MS_AS_INT(argv[1]) : (int64_t)ms_as_double(argv[1]);
        if (o < 0 || (size_t)o >= src_len) {
            ms_vm_runtime_error(vm, "binary.varint_decode: offset out of range");
            return MS_NIL_VAL();
        }
        off = (size_t)o;
    }
    uint64_t result = 0; int shift = 0;
    size_t start = off;
    while (off < src_len) {
        uint8_t b = src[off++];
        result |= (uint64_t)(b & 0x7F) << shift;
        shift += 7;
        if (!(b & 0x80)) break;
        if (shift >= 70) {
            ms_vm_runtime_error(vm, "binary.varint_decode: varint too large");
            return MS_NIL_VAL();
        }
    }
    MsObjList* list = ms_obj_list_new(vm);
    ms_value_array_push(&list->items, MS_INT_VAL((int64_t)result));
    ms_value_array_push(&list->items, MS_INT_VAL((int64_t)(off - start)));
    return MS_OBJ_VAL(list);
}

/* Signed zigzag LEB128 encode */
static MsValue binary_svarint_encode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    int64_t n = MS_IS_INT(argv[0]) ? MS_AS_INT(argv[0]) : (int64_t)ms_as_double(argv[0]);
    /* zigzag: map n→ (n<<1) ^ (n>>63) */
    uint64_t zz = (uint64_t)((n << 1) ^ (n >> 63));
    MsValue tmp_argv[1]; tmp_argv[0] = MS_INT_VAL((int64_t)zz);
    return binary_varint_encode(vm, 1, tmp_argv);
}

/* Signed zigzag LEB128 decode → [value, bytes_consumed] */
static MsValue binary_svarint_decode(MsVM* vm, int argc, MsValue* argv) {
    MsValue raw = binary_varint_decode(vm, argc, argv);
    if (MS_IS_NIL(raw)) return raw;
    MsObjList* list = MS_AS_LIST(raw);
    uint64_t zz = (uint64_t)MS_AS_INT(list->items.data[0]);
    int64_t decoded = (int64_t)((zz >> 1) ^ -(int64_t)(zz & 1));
    list->items.data[0] = MS_INT_VAL(decoded);
    return raw;
}

/* ------------------------------------------------------------------ */
/* Convenience read_uN functions                                      */
/* ------------------------------------------------------------------ */

static MsValue binary_read_u8(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t* src; size_t len;
    if (!coerce_bytes(argv[0], &src, &len)) {
        ms_vm_runtime_error(vm, "binary.read_u8: expected buffer"); return MS_NIL_VAL();
    }
    int64_t off = MS_IS_INT(argv[1]) ? MS_AS_INT(argv[1]) : (int64_t)ms_as_double(argv[1]);
    if (off < 0 || (size_t)off >= len) {
        ms_vm_runtime_error(vm, "binary.read_u8: offset out of range"); return MS_NIL_VAL();
    }
    return MS_INT_VAL((int64_t)src[off]);
}

#define DEF_READ_UN(name, fn_read, nbytes) \
static MsValue binary_read_##name(MsVM* vm, int argc, MsValue* argv) { \
    (void)argc; \
    const uint8_t* src; size_t len; \
    if (!coerce_bytes(argv[0], &src, &len)) { \
        ms_vm_runtime_error(vm, "binary.read_" #name ": expected buffer"); return MS_NIL_VAL(); \
    } \
    int64_t off = MS_IS_INT(argv[1]) ? MS_AS_INT(argv[1]) : (int64_t)ms_as_double(argv[1]); \
    if (off < 0 || (size_t)off + (nbytes) > len) { \
        ms_vm_runtime_error(vm, "binary.read_" #name ": offset out of range"); return MS_NIL_VAL(); \
    } \
    return MS_INT_VAL((int64_t)fn_read(src + (size_t)off)); \
}

DEF_READ_UN(u16_le, read_u16_le, 2)
DEF_READ_UN(u16_be, read_u16_be, 2)
DEF_READ_UN(u32_le, read_u32_le, 4)
DEF_READ_UN(u32_be, read_u32_be, 4)
DEF_READ_UN(u64_le, read_u64_le, 8)
DEF_READ_UN(u64_be, read_u64_be, 8)

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef binary_defs[] = {
    {"pack",           binary_pack,           -1},
    {"unpack",         binary_unpack,         -1},
    {"pack_into",      binary_pack_into,      -1},
    {"calc_size",      binary_calc_size,       1},
    {"varint_encode",  binary_varint_encode,   1},
    {"varint_decode",  binary_varint_decode,  -1},
    {"svarint_encode", binary_svarint_encode,  1},
    {"svarint_decode", binary_svarint_decode, -1},
    {"read_u8",        binary_read_u8,         2},
    {"read_u16_le",    binary_read_u16_le,     2},
    {"read_u16_be",    binary_read_u16_be,     2},
    {"read_u32_le",    binary_read_u32_le,     2},
    {"read_u32_be",    binary_read_u32_be,     2},
    {"read_u64_le",    binary_read_u64_le,     2},
    {"read_u64_be",    binary_read_u64_be,     2},
    {NULL, NULL, 0}
};

void ms_module_binary_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, binary_defs);
}
