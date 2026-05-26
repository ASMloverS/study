/* src/stdlib/bytes.c - STDLIB-27: bytes module
 *
 * High-level byte API delegating to ObjBuffer.
 * All functions return new buffer copies (immutable-style).
 *
 * Public API:
 *   bytes.new(n=0, fill=0)               → buffer
 *   bytes.from_str(s, encoding='utf8')   → buffer
 *   bytes.from_list(ints)                → buffer
 *   bytes.copy(buf)                      → buffer
 *   bytes.concat(buf1, buf2, ...)        → buffer
 *   bytes.repeat(buf, n)                 → buffer
 *   bytes.to_str(buf, encoding='utf8')   → str
 *   bytes.to_list(buf)                   → list[int]
 *   bytes.equal(buf1, buf2)              → bool
 *   bytes.compare(buf1, buf2)            → int  -1/0/1
 *   bytes.contains(haystack, needle)     → bool
 *   bytes.index(haystack, needle)        → int
 *   bytes.count(buf, sub)                → int
 *   bytes.slice(buf, start, end=-1)      → buffer
 *   bytes.trim(buf, byte_set)            → buffer
 *   bytes.replace(buf, old, new)         → buffer
 *   bytes.reverse(buf)                   → buffer
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/memory.h"
#include "ms/stdlib/objbuffer.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static bool coerce_buf(MsValue v, const uint8_t** out, size_t* len) {
    if (MS_IS_BUFFER(v)) {
        MsObjBuffer* b = MS_AS_BUFFER(v);
        *out = b->data;
        *len = b->len;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* bytes.new(n=0, fill=0)                                               */
/* ------------------------------------------------------------------ */
static MsValue bytes_new(MsVM* vm, int argc, MsValue* argv) {
    ms_i64 n    = (argc >= 1) ? MS_AS_INT(argv[0]) : 0;
    ms_i64 fill = (argc >= 2) ? MS_AS_INT(argv[1]) : 0;
    if (n < 0) {
        ms_vm_runtime_error(vm, "bytes.new: n must be >= 0");
        return MS_NIL_VAL();
    }
    if (fill < 0 || fill > 255) {
        ms_vm_runtime_error(vm, "bytes.new: fill must be 0-255");
        return MS_NIL_VAL();
    }
    MsObjBuffer* b = ms_obj_buffer_new(vm, (size_t)n);
    if (n > 0) {
        memset(b->data, (int)(fill & 0xFF), (size_t)n);
        b->len = (size_t)n;
    }
    return MS_OBJ_VAL(b);
}

/* ------------------------------------------------------------------ */
/* bytes.from_str(s, encoding='utf8')                                   */
/* ------------------------------------------------------------------ */
static MsValue bytes_from_str(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "bytes.from_str: expected string");
        return MS_NIL_VAL();
    }
    if (argc >= 2) {
        if (!MS_IS_STRING(argv[1])) {
            ms_vm_runtime_error(vm, "bytes.from_str: encoding must be a string");
            return MS_NIL_VAL();
        }
        MsObjString* enc = MS_AS_STRING(argv[1]);
        if (strcmp(enc->data, "utf8") != 0 && strcmp(enc->data, "utf-8") != 0) {
            ms_vm_runtime_error(vm, "bytes.from_str: unsupported encoding '%s'", enc->data);
            return MS_NIL_VAL();
        }
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm,
        (const uint8_t*)s->data, (size_t)s->length));
}

/* ------------------------------------------------------------------ */
/* bytes.from_list(ints)                                                */
/* ------------------------------------------------------------------ */
static MsValue bytes_from_list(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "bytes.from_list: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* lst = MS_AS_LIST(argv[0]);
    size_t n = lst->items.count;
    MsObjBuffer* b = ms_obj_buffer_new(vm, n == 0 ? 0 : n);
    for (size_t i = 0; i < n; i++) {
        MsValue v = lst->items.data[i];
        ms_i64  iv = MS_IS_INT(v) ? MS_AS_INT(v) : (ms_i64)MS_AS_NUMBER(v);
        if (iv < 0 || iv > 255) {
            ms_vm_runtime_error(vm,
                "bytes.from_list: value %lld out of range [0,255]", (long long)iv);
            return MS_NIL_VAL();
        }
        b->data[i] = (uint8_t)iv;
    }
    b->len = n;
    return MS_OBJ_VAL(b);
}

/* ------------------------------------------------------------------ */
/* bytes.copy(buf)                                                      */
/* ------------------------------------------------------------------ */
static MsValue bytes_copy(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BUFFER(argv[0])) {
        ms_vm_runtime_error(vm, "bytes.copy: expected buffer");
        return MS_NIL_VAL();
    }
    MsObjBuffer* src = MS_AS_BUFFER(argv[0]);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, src->data, src->len));
}

/* ------------------------------------------------------------------ */
/* bytes.concat(buf1, buf2, ...)                                        */
/* ------------------------------------------------------------------ */
static MsValue bytes_concat(MsVM* vm, int argc, MsValue* argv) {
    size_t total = 0;
    for (int i = 0; i < argc; i++) {
        if (!MS_IS_BUFFER(argv[i])) {
            ms_vm_runtime_error(vm, "bytes.concat: all arguments must be buffers");
            return MS_NIL_VAL();
        }
        total += MS_AS_BUFFER(argv[i])->len;
    }
    MsObjBuffer* nb = ms_obj_buffer_new(vm, total == 0 ? 0 : total);
    size_t off = 0;
    for (int i = 0; i < argc; i++) {
        MsObjBuffer* src = MS_AS_BUFFER(argv[i]);
        if (src->len > 0) {
            memcpy(nb->data + off, src->data, src->len);
            off += src->len;
        }
    }
    nb->len = total;
    return MS_OBJ_VAL(nb);
}

/* ------------------------------------------------------------------ */
/* bytes.repeat(buf, n)                                                 */
/* ------------------------------------------------------------------ */
static MsValue bytes_repeat(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BUFFER(argv[0])) {
        ms_vm_runtime_error(vm, "bytes.repeat: first argument must be a buffer");
        return MS_NIL_VAL();
    }
    ms_i64 n = MS_IS_INT(argv[1]) ? MS_AS_INT(argv[1]) : (ms_i64)MS_AS_NUMBER(argv[1]);
    if (n < 0) {
        ms_vm_runtime_error(vm, "bytes.repeat: n must be >= 0");
        return MS_NIL_VAL();
    }
    MsObjBuffer* src = MS_AS_BUFFER(argv[0]);
    size_t total = src->len * (size_t)n;
    MsObjBuffer* nb = ms_obj_buffer_new(vm, total == 0 ? 0 : total);
    for (ms_i64 i = 0; i < n; i++)
        if (src->len > 0)
            memcpy(nb->data + (size_t)i * src->len, src->data, src->len);
    nb->len = total;
    return MS_OBJ_VAL(nb);
}

/* ------------------------------------------------------------------ */
/* bytes.to_str(buf, encoding='utf8')                                   */
/* ------------------------------------------------------------------ */
static MsValue bytes_to_str(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_BUFFER(argv[0])) {
        ms_vm_runtime_error(vm, "bytes.to_str: expected buffer");
        return MS_NIL_VAL();
    }
    if (argc >= 2) {
        if (!MS_IS_STRING(argv[1])) {
            ms_vm_runtime_error(vm, "bytes.to_str: encoding must be a string");
            return MS_NIL_VAL();
        }
        MsObjString* enc = MS_AS_STRING(argv[1]);
        if (strcmp(enc->data, "utf8") != 0 && strcmp(enc->data, "utf-8") != 0) {
            ms_vm_runtime_error(vm, "bytes.to_str: unsupported encoding '%s'", enc->data);
            return MS_NIL_VAL();
        }
    }
    MsObjBuffer* b = MS_AS_BUFFER(argv[0]);
    return MS_OBJ_VAL(ms_obj_string_copy(vm, (const char*)b->data, (int)b->len));
}

/* ------------------------------------------------------------------ */
/* bytes.to_list(buf)                                                   */
/* ------------------------------------------------------------------ */
static MsValue bytes_to_list(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BUFFER(argv[0])) {
        ms_vm_runtime_error(vm, "bytes.to_list: expected buffer");
        return MS_NIL_VAL();
    }
    MsObjBuffer* b = MS_AS_BUFFER(argv[0]);
    MsObjList* lst = ms_obj_list_new(vm);
    for (size_t i = 0; i < b->len; i++)
        ms_value_array_push(&lst->items, MS_INT_VAL((ms_i64)b->data[i]));
    return MS_OBJ_VAL(lst);
}

/* ------------------------------------------------------------------ */
/* bytes.equal(buf1, buf2)                                              */
/* ------------------------------------------------------------------ */
static MsValue bytes_equal(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t *a, *b;
    size_t la, lb;
    if (!coerce_buf(argv[0], &a, &la) || !coerce_buf(argv[1], &b, &lb)) {
        ms_vm_runtime_error(vm, "bytes.equal: arguments must be buffers");
        return MS_NIL_VAL();
    }
    bool eq = (la == lb) && (la == 0 || memcmp(a, b, la) == 0);
    return MS_BOOL_VAL(eq);
}

/* ------------------------------------------------------------------ */
/* bytes.compare(buf1, buf2)                                            */
/* ------------------------------------------------------------------ */
static MsValue bytes_compare(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t *a, *b;
    size_t la, lb;
    if (!coerce_buf(argv[0], &a, &la) || !coerce_buf(argv[1], &b, &lb)) {
        ms_vm_runtime_error(vm, "bytes.compare: arguments must be buffers");
        return MS_NIL_VAL();
    }
    size_t mn = la < lb ? la : lb;
    int cmp = (mn > 0) ? memcmp(a, b, mn) : 0;
    if (cmp < 0 || (cmp == 0 && la < lb)) return MS_INT_VAL(-1);
    if (cmp > 0 || (cmp == 0 && la > lb)) return MS_INT_VAL(1);
    return MS_INT_VAL(0);
}

/* ------------------------------------------------------------------ */
/* bytes.index(haystack, needle)  — first occurrence, or -1            */
/* ------------------------------------------------------------------ */
static ms_i64 buf_find(const uint8_t* hay, size_t hlen,
                        const uint8_t* needle, size_t nlen) {
    if (nlen == 0) return 0;
    if (nlen > hlen) return -1;
    for (size_t i = 0; i + nlen <= hlen; i++)
        if (memcmp(hay + i, needle, nlen) == 0)
            return (ms_i64)i;
    return -1;
}

static MsValue bytes_index(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t *h, *n;
    size_t lh, ln;
    if (!coerce_buf(argv[0], &h, &lh) || !coerce_buf(argv[1], &n, &ln)) {
        ms_vm_runtime_error(vm, "bytes.index: arguments must be buffers");
        return MS_NIL_VAL();
    }
    return MS_INT_VAL(buf_find(h, lh, n, ln));
}

/* ------------------------------------------------------------------ */
/* bytes.contains(haystack, needle)                                     */
/* ------------------------------------------------------------------ */
static MsValue bytes_contains(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t *h, *n;
    size_t lh, ln;
    if (!coerce_buf(argv[0], &h, &lh) || !coerce_buf(argv[1], &n, &ln)) {
        ms_vm_runtime_error(vm, "bytes.contains: arguments must be buffers");
        return MS_NIL_VAL();
    }
    return MS_BOOL_VAL(buf_find(h, lh, n, ln) >= 0);
}

/* ------------------------------------------------------------------ */
/* bytes.count(buf, sub) — non-overlapping occurrences                  */
/* ------------------------------------------------------------------ */
static MsValue bytes_count(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const uint8_t *h, *n;
    size_t lh, ln;
    if (!coerce_buf(argv[0], &h, &lh) || !coerce_buf(argv[1], &n, &ln)) {
        ms_vm_runtime_error(vm, "bytes.count: arguments must be buffers");
        return MS_NIL_VAL();
    }
    if (ln == 0) return MS_INT_VAL((ms_i64)(lh + 1));
    ms_i64 cnt = 0;
    size_t i = 0;
    while (ln <= lh && i + ln <= lh) {
        if (memcmp(h + i, n, ln) == 0) { cnt++; i += ln; }
        else i++;
    }
    return MS_INT_VAL(cnt);
}

/* ------------------------------------------------------------------ */
/* bytes.slice(buf, start, end=-1)                                      */
/* ------------------------------------------------------------------ */
static MsValue bytes_slice(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_BUFFER(argv[0])) {
        ms_vm_runtime_error(vm, "bytes.slice: expected buffer");
        return MS_NIL_VAL();
    }
    MsObjBuffer* b = MS_AS_BUFFER(argv[0]);
    ms_i64 s = MS_IS_INT(argv[1]) ? MS_AS_INT(argv[1]) : 0;
    ms_i64 e = (argc >= 3) ? (MS_IS_INT(argv[2]) ? MS_AS_INT(argv[2]) : -1) : -1;
    if (s < 0) s = 0;
    if (e < 0 || (size_t)e > b->len) e = (ms_i64)b->len;
    if (s >= e) return MS_OBJ_VAL(ms_obj_buffer_new(vm, 0));
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, b->data + s, (size_t)(e - s)));
}

/* ------------------------------------------------------------------ */
/* bytes.trim(buf, byte_set) — strip leading/trailing bytes in set      */
/* ------------------------------------------------------------------ */
static MsValue bytes_trim(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BUFFER(argv[0]) || !MS_IS_BUFFER(argv[1])) {
        ms_vm_runtime_error(vm, "bytes.trim: arguments must be buffers");
        return MS_NIL_VAL();
    }
    MsObjBuffer* b   = MS_AS_BUFFER(argv[0]);
    MsObjBuffer* set = MS_AS_BUFFER(argv[1]);

    /* build lookup table */
    bool in_set[256] = {false};
    for (size_t i = 0; i < set->len; i++)
        in_set[set->data[i]] = true;

    size_t lo = 0, hi = b->len;
    while (lo < hi && in_set[b->data[lo]])   lo++;
    while (hi > lo && in_set[b->data[hi-1]]) hi--;
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, b->data + lo, hi - lo));
}

/* ------------------------------------------------------------------ */
/* bytes.replace(buf, old, new) — all occurrences                       */
/* ------------------------------------------------------------------ */
static MsValue bytes_replace(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BUFFER(argv[0]) || !MS_IS_BUFFER(argv[1]) || !MS_IS_BUFFER(argv[2])) {
        ms_vm_runtime_error(vm, "bytes.replace: arguments must be buffers");
        return MS_NIL_VAL();
    }
    MsObjBuffer* src  = MS_AS_BUFFER(argv[0]);
    MsObjBuffer* pat  = MS_AS_BUFFER(argv[1]);
    MsObjBuffer* repl = MS_AS_BUFFER(argv[2]);

    if (pat->len == 0 || src->len < pat->len) {
        /* no-op: return copy */
        return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, src->data, src->len));
    }
    MsObjBuffer* nb = ms_obj_buffer_new(vm, src->len);

    size_t i = 0;
    while (i + pat->len <= src->len) {
        if (memcmp(src->data + i, pat->data, pat->len) == 0) {
            if (repl->len > 0) {
                size_t need = nb->len + repl->len;
                if (nb->cap < need) {
                    size_t nc = nb->cap < 8 ? 8 : nb->cap;
                    while (nc < need) nc *= 2;
                    nb->data = (uint8_t*)ms_reallocate(vm, nb->data, nb->cap, nc);
                    nb->cap = nc;
                }
                memcpy(nb->data + nb->len, repl->data, repl->len);
                nb->len += repl->len;
            }
            i += pat->len;
        } else {
            if (nb->len >= nb->cap) {
                size_t nc = nb->cap < 8 ? 8 : nb->cap * 2;
                nb->data = (uint8_t*)ms_reallocate(vm, nb->data, nb->cap, nc);
                nb->cap = nc;
            }
            nb->data[nb->len++] = src->data[i++];
        }
    }
    /* tail */
    if (i < src->len) {
        size_t tail = src->len - i;
        size_t need = nb->len + tail;
        if (nb->cap < need) {
            size_t nc = nb->cap < 8 ? 8 : nb->cap;
            while (nc < need) nc *= 2;
            nb->data = (uint8_t*)ms_reallocate(vm, nb->data, nb->cap, nc);
            nb->cap = nc;
        }
        memcpy(nb->data + nb->len, src->data + i, tail);
        nb->len += tail;
    }
    return MS_OBJ_VAL(nb);
}

/* ------------------------------------------------------------------ */
/* bytes.reverse(buf) — returns new copy with reversed byte order       */
/* ------------------------------------------------------------------ */
static MsValue bytes_reverse(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BUFFER(argv[0])) {
        ms_vm_runtime_error(vm, "bytes.reverse: expected buffer");
        return MS_NIL_VAL();
    }
    MsObjBuffer* src = MS_AS_BUFFER(argv[0]);
    MsObjBuffer* nb  = ms_obj_buffer_from_bytes(vm, src->data, src->len);
    /* reverse in place */
    for (size_t lo = 0, hi = nb->len; lo + 1 < hi; lo++, hi--) {
        uint8_t t = nb->data[lo];
        nb->data[lo] = nb->data[hi - 1];
        nb->data[hi - 1] = t;
    }
    return MS_OBJ_VAL(nb);
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */
void ms_module_bytes_init(MsVM* vm, MsObjModule* mod) {
    static const MsNativeDef defs[] = {
        {"new",       bytes_new,       -1},
        {"from_str",  bytes_from_str,  -1},
        {"from_list", bytes_from_list,  1},
        {"copy",      bytes_copy,       1},
        {"concat",    bytes_concat,    -1},
        {"repeat",    bytes_repeat,     2},
        {"to_str",    bytes_to_str,    -1},
        {"to_list",   bytes_to_list,    1},
        {"equal",     bytes_equal,      2},
        {"compare",   bytes_compare,    2},
        {"contains",  bytes_contains,   2},
        {"index",     bytes_index,      2},
        {"count",     bytes_count,      2},
        {"slice",     bytes_slice,     -1},
        {"trim",      bytes_trim,       2},
        {"replace",   bytes_replace,    3},
        {"reverse",   bytes_reverse,    1},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);
}
