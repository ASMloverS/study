/* src/stdlib/json.c - STDLIB-16: json module
 *
 * json.encode(value)              → str
 * json.encode_pretty(value, indent=2) → str
 * json.decode(s)                  → any   (runtime error on bad JSON)
 * json.try_decode(s)              → any|nil
 * json.is_valid(s)                → bool
 *
 * Pure C, no external deps.  RFC 7159.
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/table_types.h"
#include "ms/vtable.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Dynamic string buffer                                               */
/* ------------------------------------------------------------------ */

typedef struct { char* data; size_t len; size_t cap; } JBuf;

static void jb_init(JBuf* b) {
    b->cap = 64; b->len = 0;
    b->data = (char*)malloc(b->cap);
    if (!b->data) abort();
}

static void jb_free(JBuf* b) { free(b->data); }

static void jb_reserve(JBuf* b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    while (b->len + extra > b->cap) b->cap *= 2;
    b->data = (char*)realloc(b->data, b->cap);
    if (!b->data) abort();
}

static void jb_append(JBuf* b, const char* s, size_t n) {
    jb_reserve(b, n + 1);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void jb_appendc(JBuf* b, char c) { jb_append(b, &c, 1); }

/* ------------------------------------------------------------------ */
/* Cycle detection: lightweight pointer set (addresses of list/map)   */
/* ------------------------------------------------------------------ */

typedef struct { void** ptrs; int count; int cap; } PtrSet;

static void ps_init(PtrSet* s) {
    s->cap = 8; s->count = 0;
    s->ptrs = (void**)malloc(sizeof(void*) * (size_t)s->cap);
    if (!s->ptrs) abort();
}

static void ps_free(PtrSet* s) { free(s->ptrs); }

static bool ps_contains(PtrSet* s, void* p) {
    for (int i = 0; i < s->count; i++) if (s->ptrs[i] == p) return true;
    return false;
}

static void ps_push(PtrSet* s, void* p) {
    if (s->count >= s->cap) {
        s->cap *= 2;
        s->ptrs = (void**)realloc(s->ptrs, sizeof(void*) * (size_t)s->cap);
        if (!s->ptrs) abort();
    }
    s->ptrs[s->count++] = p;
}

static void ps_pop(PtrSet* s) { if (s->count > 0) s->count--; }

/* ------------------------------------------------------------------ */
/* Encoder                                                             */
/* ------------------------------------------------------------------ */

static const char hex_digits[] = "0123456789abcdef";

static void encode_string(JBuf* b, const char* s, int len) {
    jb_appendc(b, '"');
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  jb_append(b, "\\\"", 2); break;
            case '\\': jb_append(b, "\\\\", 2); break;
            case '\n': jb_append(b, "\\n",  2); break;
            case '\r': jb_append(b, "\\r",  2); break;
            case '\t': jb_append(b, "\\t",  2); break;
            default:
                if (c < 0x20) {
                    char esc[7];
                    esc[0]='\\'; esc[1]='u'; esc[2]='0'; esc[3]='0';
                    esc[4]=hex_digits[c>>4]; esc[5]=hex_digits[c&0xF]; esc[6]='\0';
                    jb_append(b, esc, 6);
                } else {
                    jb_appendc(b, (char)c);
                }
                break;
        }
    }
    jb_appendc(b, '"');
}

/* Forward declaration for mutual recursion */
static bool encode_value(MsVM* vm, JBuf* b, MsValue v, PtrSet* seen,
                          int indent, int depth, bool pretty);

static bool encode_list(MsVM* vm, JBuf* b, MsObjList* lst, PtrSet* seen,
                         int indent, int depth, bool pretty) {
    if (ps_contains(seen, lst)) {
        ms_vm_runtime_error(vm, "json.encode: circular reference in list");
        return false;
    }
    ps_push(seen, lst);
    jb_appendc(b, '[');
    int n = lst->items.count;
    for (int i = 0; i < n; i++) {
        if (i > 0) jb_appendc(b, ',');
        if (pretty) {
            jb_appendc(b, '\n');
            for (int d = 0; d <= depth; d++)
                for (int sp = 0; sp < indent; sp++) jb_appendc(b, ' ');
        }
        if (!encode_value(vm, b, lst->items.data[i], seen, indent, depth + 1, pretty))
            return false;
    }
    if (pretty && n > 0) {
        jb_appendc(b, '\n');
        for (int d = 0; d < depth; d++)
            for (int sp = 0; sp < indent; sp++) jb_appendc(b, ' ');
    }
    jb_appendc(b, ']');
    ps_pop(seen);
    return true;
}

static bool encode_map(MsVM* vm, JBuf* b, MsObjMap* map, PtrSet* seen,
                        int indent, int depth, bool pretty) {
    if (ps_contains(seen, map)) {
        ms_vm_runtime_error(vm, "json.encode: circular reference in map");
        return false;
    }
    ps_push(seen, map);
    jb_appendc(b, '{');
    MsValueTable* t = &map->table;
    bool first = true;
    int written = 0;
    for (int i = 0; i < t->capacity; i++) {
        MsVEntry* e = &t->entries[i];
        if (!e->used || e->tombstone) continue;
        if (!MS_IS_STRING(e->key)) {
            ms_vm_runtime_error(vm, "json.encode: map key must be string");
            return false;
        }
        if (!first) jb_appendc(b, ',');
        first = false;
        if (pretty) {
            jb_appendc(b, '\n');
            for (int d = 0; d <= depth; d++)
                for (int sp = 0; sp < indent; sp++) jb_appendc(b, ' ');
        }
        MsObjString* key = MS_AS_STRING(e->key);
        encode_string(b, key->data, key->length);
        jb_appendc(b, ':');
        if (pretty) jb_appendc(b, ' ');
        if (!encode_value(vm, b, e->value, seen, indent, depth + 1, pretty))
            return false;
        written++;
    }
    if (pretty && written > 0) {
        jb_appendc(b, '\n');
        for (int d = 0; d < depth; d++)
            for (int sp = 0; sp < indent; sp++) jb_appendc(b, ' ');
    }
    jb_appendc(b, '}');
    ps_pop(seen);
    return true;
}

static bool encode_value(MsVM* vm, JBuf* b, MsValue v, PtrSet* seen,
                          int indent, int depth, bool pretty) {
    if (MS_IS_NIL(v)) { jb_append(b, "null", 4); return true; }
    if (MS_IS_BOOL(v)) {
        if (MS_AS_BOOL(v)) jb_append(b, "true", 4);
        else               jb_append(b, "false", 5);
        return true;
    }
    if (MS_IS_INT(v)) {
        char tmp[32];
        int n = snprintf(tmp, sizeof(tmp), "%" PRId64, (int64_t)MS_AS_INT(v));
        jb_append(b, tmp, (size_t)n);
        return true;
    }
    if (MS_IS_NUMBER(v)) {
        double d = MS_AS_NUMBER(v);
        if (isnan(d)) { ms_vm_runtime_error(vm, "json.encode: NaN is not valid JSON"); return false; }
        if (isinf(d)) { ms_vm_runtime_error(vm, "json.encode: Infinity is not valid JSON"); return false; }
        char tmp[64];
        int n = snprintf(tmp, sizeof(tmp), "%.17g", d);
        jb_append(b, tmp, (size_t)n);
        return true;
    }
    if (MS_IS_STRING(v)) {
        MsObjString* s = MS_AS_STRING(v);
        encode_string(b, s->data, s->length);
        return true;
    }
    if (MS_IS_LIST(v)) return encode_list(vm, b, MS_AS_LIST(v), seen, indent, depth, pretty);
    if (MS_IS_MAP(v))  return encode_map(vm, b, MS_AS_MAP(v), seen, indent, depth, pretty);
    ms_vm_runtime_error(vm, "json.encode: unsupported value type");
    return false;
}

/* ------------------------------------------------------------------ */
/* Decoder                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* src;
    int         len;
    int         pos;
    /* error state */
    bool        error;
    char        errmsg[128];
} JParser;

static void jp_error(JParser* p, const char* msg) {
    if (!p->error) {
        p->error = true;
        snprintf(p->errmsg, sizeof(p->errmsg), "%s (at offset %d)", msg, p->pos);
    }
}

static void jp_skip_ws(JParser* p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->pos++;
        else break;
    }
}

static char jp_peek(JParser* p) {
    jp_skip_ws(p);
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos];
}

static char jp_next(JParser* p) {
    jp_skip_ws(p);
    if (p->pos >= p->len) { jp_error(p, "unexpected end of input"); return '\0'; }
    return p->src[p->pos++];
}

static bool jp_expect(JParser* p, char c) {
    char got = jp_next(p);
    if (got != c) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected '%c', got '%c'", c, got);
        jp_error(p, msg);
        return false;
    }
    return true;
}

/* Forward declaration */
static MsValue jp_parse_value(MsVM* vm, JParser* p);

/* Parse \uXXXX, write UTF-8 into buf, return bytes written (0..4) */
static int parse_unicode_escape(JParser* p, char* buf) {
    if (p->pos + 4 > p->len) { jp_error(p, "truncated \\u escape"); return 0; }
    uint32_t cp = 0;
    for (int i = 0; i < 4; i++) {
        char c = p->src[p->pos++];
        uint32_t nibble;
        if (c >= '0' && c <= '9') nibble = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nibble = (uint32_t)(c - 'A' + 10);
        else { jp_error(p, "invalid hex in \\u escape"); return 0; }
        cp = (cp << 4) | nibble;
    }
    /* Handle surrogate pair */
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (p->pos + 6 <= p->len &&
            p->src[p->pos] == '\\' && p->src[p->pos+1] == 'u') {
            p->pos += 2;
            uint32_t low = 0;
            for (int i = 0; i < 4; i++) {
                char c = p->src[p->pos++];
                uint32_t nibble;
                if (c >= '0' && c <= '9') nibble = (uint32_t)(c - '0');
                else if (c >= 'a' && c <= 'f') nibble = (uint32_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') nibble = (uint32_t)(c - 'A' + 10);
                else { jp_error(p, "invalid hex in surrogate low"); return 0; }
                low = (low << 4) | nibble;
            }
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else { jp_error(p, "invalid surrogate pair"); return 0; }
        }
    }
    /* Encode as UTF-8 */
    if (cp <= 0x7F) {
        buf[0] = (char)cp; return 1;
    } else if (cp <= 0x7FF) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F)); return 2;
    } else if (cp <= 0xFFFF) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F)); return 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6)  & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F)); return 4;
    }
}

static MsValue jp_parse_string(MsVM* vm, JParser* p) {
    /* caller already consumed the opening '"' */
    JBuf b; jb_init(&b);
    while (!p->error) {
        if (p->pos >= p->len) { jp_error(p, "unterminated string"); break; }
        char c = p->src[p->pos++];
        if (c == '"') break;
        if (c == '\\') {
            if (p->pos >= p->len) { jp_error(p, "truncated escape"); break; }
            char esc = p->src[p->pos++];
            switch (esc) {
                case '"':  jb_appendc(&b, '"');  break;
                case '\\': jb_appendc(&b, '\\'); break;
                case '/':  jb_appendc(&b, '/');  break;
                case 'n':  jb_appendc(&b, '\n'); break;
                case 'r':  jb_appendc(&b, '\r'); break;
                case 't':  jb_appendc(&b, '\t'); break;
                case 'b':  jb_appendc(&b, '\b'); break;
                case 'f':  jb_appendc(&b, '\f'); break;
                case 'u': {
                    char utf[4]; int n = parse_unicode_escape(p, utf);
                    if (n) jb_append(&b, utf, (size_t)n);
                    break;
                }
                default: jp_error(p, "invalid escape sequence"); break;
            }
        } else {
            jb_appendc(&b, c);
        }
    }
    MsValue result;
    if (p->error) {
        result = MS_NIL_VAL();
    } else {
        MsObjString* s = ms_obj_string_copy(vm, b.data, (int)b.len);
        result = MS_OBJ_VAL(s);
    }
    jb_free(&b);
    return result;
}

static MsValue jp_parse_number(JParser* p) {
    /* parse sign + digits (already at start of number, no leading ws) */
    int start = p->pos;
    if (p->pos < p->len && p->src[p->pos] == '-') p->pos++;
    while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    bool is_float = false;
    if (p->pos < p->len && p->src[p->pos] == '.') { is_float = true; p->pos++;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    }
    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        is_float = true; p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-')) p->pos++;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    }
    int end = p->pos;
    if (end == start) { jp_error(p, "invalid number"); return MS_NIL_VAL(); }

    char tmp[64];
    int nlen = end - start;
    if (nlen >= 63) { jp_error(p, "number too long"); return MS_NIL_VAL(); }
    memcpy(tmp, p->src + start, (size_t)nlen);
    tmp[nlen] = '\0';

    if (!is_float) {
        /* Try int64 first */
        char* endptr;
        errno = 0;
        int64_t iv = (int64_t)strtoll(tmp, &endptr, 10);
        if (errno == 0 && endptr == tmp + nlen) return MS_INT_VAL((ms_i64)iv);
        /* Overflow: fall through to double */
    }
    char* endptr;
    double dv = strtod(tmp, &endptr);
    if (endptr == tmp) { jp_error(p, "invalid number"); return MS_NIL_VAL(); }
    return MS_NUMBER_VAL(dv);
}

static MsValue jp_parse_array(MsVM* vm, JParser* p) {
    /* caller consumed '[' */
    MsObjList* lst = ms_obj_list_new(vm);
    MsValue lv = MS_OBJ_VAL(lst);
    if (jp_peek(p) == ']') { p->pos++; return lv; }
    while (!p->error) {
        MsValue item = jp_parse_value(vm, p);
        if (p->error) return MS_NIL_VAL();
        ms_value_array_push(&lst->items, item);
        char c = jp_peek(p);
        if (c == ']') { p->pos++; break; }
        if (c != ',') { jp_error(p, "expected ',' or ']' in array"); break; }
        p->pos++;
    }
    return p->error ? MS_NIL_VAL() : lv;
}

static MsValue jp_parse_object(MsVM* vm, JParser* p) {
    /* caller consumed '{' */
    MsObjMap* map = ms_obj_map_new(vm);
    MsValue mv = MS_OBJ_VAL(map);
    if (jp_peek(p) == '}') { p->pos++; return mv; }
    while (!p->error) {
        char q = jp_next(p);
        if (q != '"') { jp_error(p, "expected string key in object"); break; }
        MsValue key = jp_parse_string(vm, p);
        if (p->error) break;
        jp_expect(p, ':');
        if (p->error) break;
        MsValue val = jp_parse_value(vm, p);
        if (p->error) break;
        ms_vtable_set(&map->table, key, val);
        char c = jp_peek(p);
        if (c == '}') { p->pos++; break; }
        if (c != ',') { jp_error(p, "expected ',' or '}' in object"); break; }
        p->pos++;
    }
    return p->error ? MS_NIL_VAL() : mv;
}

static MsValue jp_parse_value(MsVM* vm, JParser* p) {
    char c = jp_peek(p);
    if (p->error) return MS_NIL_VAL();
    switch (c) {
        case '"':  p->pos++; return jp_parse_string(vm, p);
        case '[':  p->pos++; return jp_parse_array(vm, p);
        case '{':  p->pos++; return jp_parse_object(vm, p);
        case 't':
            if (p->pos + 4 <= p->len && memcmp(p->src + p->pos, "true", 4) == 0)
                { p->pos += 4; return MS_BOOL_VAL(true); }
            jp_error(p, "invalid literal"); return MS_NIL_VAL();
        case 'f':
            if (p->pos + 5 <= p->len && memcmp(p->src + p->pos, "false", 5) == 0)
                { p->pos += 5; return MS_BOOL_VAL(false); }
            jp_error(p, "invalid literal"); return MS_NIL_VAL();
        case 'n':
            if (p->pos + 4 <= p->len && memcmp(p->src + p->pos, "null", 4) == 0)
                { p->pos += 4; return MS_NIL_VAL(); }
            jp_error(p, "invalid literal"); return MS_NIL_VAL();
        case '-': case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return jp_parse_number(p);
        case '\0':
            jp_error(p, "unexpected end of input"); return MS_NIL_VAL();
        default: {
            char msg[32];
            snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
            jp_error(p, msg);
            return MS_NIL_VAL();
        }
    }
}

/* Returns MS_NIL_VAL and sets p->error on failure.
   On success, all input must be consumed (trailing whitespace allowed). */
static MsValue json_parse(MsVM* vm, const char* src, int len, JParser* p) {
    p->src = src; p->len = len; p->pos = 0; p->error = false; p->errmsg[0] = '\0';
    if (len == 0) { jp_error(p, "empty input"); return MS_NIL_VAL(); }
    MsValue v = jp_parse_value(vm, p);
    if (!p->error) {
        jp_skip_ws(p);
        if (p->pos != p->len) jp_error(p, "trailing garbage after JSON value");
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* Native function implementations                                     */
/* ------------------------------------------------------------------ */

static MsValue native_encode(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) { ms_vm_runtime_error(vm, "json.encode: expected 1 argument"); return MS_NIL_VAL(); }
    JBuf b; jb_init(&b);
    PtrSet seen; ps_init(&seen);
    bool ok = encode_value(vm, &b, argv[0], &seen, 2, 0, false);
    ps_free(&seen);
    MsValue result = MS_NIL_VAL();
    if (ok) {
        MsObjString* s = ms_obj_string_copy(vm, b.data, (int)b.len);
        result = MS_OBJ_VAL(s);
    }
    jb_free(&b);
    return result;
}

static MsValue native_encode_pretty(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) { ms_vm_runtime_error(vm, "json.encode_pretty: expected at least 1 argument"); return MS_NIL_VAL(); }
    int indent = 2;
    if (argc >= 2) {
        if (!MS_IS_INT(argv[1])) { ms_vm_runtime_error(vm, "json.encode_pretty: indent must be int"); return MS_NIL_VAL(); }
        indent = (int)MS_AS_INT(argv[1]);
        if (indent < 0) indent = 0;
    }
    JBuf b; jb_init(&b);
    PtrSet seen; ps_init(&seen);
    bool ok = encode_value(vm, &b, argv[0], &seen, indent, 0, true);
    ps_free(&seen);
    MsValue result = MS_NIL_VAL();
    if (ok) {
        MsObjString* s = ms_obj_string_copy(vm, b.data, (int)b.len);
        result = MS_OBJ_VAL(s);
    }
    jb_free(&b);
    return result;
}

static MsValue native_decode(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "json.decode: expected string argument");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    JParser p;
    MsValue v = json_parse(vm, s->data, s->length, &p);
    if (p.error) { ms_vm_runtime_error(vm, "json.decode: %s", p.errmsg); return MS_NIL_VAL(); }
    return v;
}

static MsValue native_try_decode(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) return MS_NIL_VAL();
    MsObjString* s = MS_AS_STRING(argv[0]);
    JParser p;
    MsValue v = json_parse(vm, s->data, s->length, &p);
    if (p.error) return MS_NIL_VAL();
    return v;
}

static MsValue native_is_valid(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) return MS_BOOL_VAL(false);
    MsObjString* s = MS_AS_STRING(argv[0]);
    JParser p;
    json_parse(vm, s->data, s->length, &p);
    return MS_BOOL_VAL(!p.error);
}

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

void ms_module_json_init(MsVM* vm, MsObjModule* mod) {
    static const MsNativeDef defs[] = {
        { "encode",        native_encode,        1  },
        { "encode_pretty", native_encode_pretty, -1 },
        { "decode",        native_decode,        1  },
        { "try_decode",    native_try_decode,    1  },
        { "is_valid",      native_is_valid,      1  },
        { NULL, NULL, 0 }
    };
    ms_module_register_natives(vm, mod, defs);
}
