/* src/stdlib/_strconv.c - STDLIB-14: strconv module (C primitive layer)
 *
 * Locale-independent string <-> basic type conversions:
 *   format_int, parse_int, try_parse_int,
 *   format_float, parse_float, try_parse_float,
 *   parse_bool, format_bool,
 *   quote, unquote, quote_rune.
 *
 * Registered under the module name "strconv".
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static bool req_str(MsVM* vm, MsValue v, const char* fn, MsObjString** out) {
    if (!MS_IS_STRING(v)) {
        ms_vm_runtime_error(vm, "%s: expected string", fn);
        return false;
    }
    *out = MS_AS_STRING(v);
    return true;
}

static bool req_int(MsVM* vm, MsValue v, const char* fn, ms_i64* out) {
    if (!MS_IS_INT(v)) {
        ms_vm_runtime_error(vm, "%s: expected int", fn);
        return false;
    }
    *out = MS_AS_INT(v);
    return true;
}

/* Convert single character escape sequence, writing to *out.
   Returns number of chars consumed from src (1 or 2).
   Returns 0 if not a valid escape. */
static int unescape_one(const char* src, char* out) {
    if (src[0] != '\\') { *out = src[0]; return 1; }
    switch (src[1]) {
        case 'n':  *out = '\n'; return 2;
        case 't':  *out = '\t'; return 2;
        case 'r':  *out = '\r'; return 2;
        case '\\': *out = '\\'; return 2;
        case '"':  *out = '"';  return 2;
        case '\'': *out = '\''; return 2;
        case '0':  *out = '\0'; return 2;
        default:   return 0;
    }
}

/* Write escape sequence for char c into buf (always room for 4 bytes). */
static int escape_char(char c, char* buf) {
    switch (c) {
        case '\n': buf[0]='\\'; buf[1]='n';  return 2;
        case '\t': buf[0]='\\'; buf[1]='t';  return 2;
        case '\r': buf[0]='\\'; buf[1]='r';  return 2;
        case '\\': buf[0]='\\'; buf[1]='\\'; return 2;
        case '"':  buf[0]='\\'; buf[1]='"';  return 2;
        case '\'': buf[0]='\\'; buf[1]='\''; return 2;
        default:
            if ((unsigned char)c < 0x20) {
                /* \xHH */
                buf[0]='\\'; buf[1]='x';
                static const char hex[]="0123456789abcdef";
                buf[2]=hex[((unsigned char)c)>>4];
                buf[3]=hex[((unsigned char)c)&0xF];
                return 4;
            }
            buf[0] = c;
            return 1;
    }
}

/* ------------------------------------------------------------------ */
/* format_int(n, base=10) → str                                        */
/* ------------------------------------------------------------------ */

static MsValue sc_format_int(MsVM* vm, int argc, MsValue* argv) {
    ms_i64 n;
    if (!req_int(vm, argv[0], "strconv.format_int", &n)) return MS_NIL_VAL();
    ms_i64 base = 10;
    if (argc >= 2 && MS_IS_INT(argv[1])) base = MS_AS_INT(argv[1]);
    if (base < 2 || base > 36) {
        ms_vm_runtime_error(vm, "strconv.format_int: base must be 2..36");
        return MS_NIL_VAL();
    }

    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char buf[72]; /* enough for base-2 i64 */
    int pos = (int)sizeof(buf) - 1;
    buf[pos] = '\0';

    bool neg = (n < 0);
    /* use unsigned arithmetic to avoid UB on INT64_MIN */
    ms_u64 un = neg ? (ms_u64)(-(n + 1)) + 1ULL : (ms_u64)n;
    if (un == 0) { buf[--pos] = '0'; }
    else {
        while (un > 0) {
            buf[--pos] = digits[(int)(un % (ms_u64)base)];
            un /= (ms_u64)base;
        }
    }
    if (neg) buf[--pos] = '-';

    return MS_OBJ_VAL(ms_obj_string_copy(vm, buf + pos,
                                          (int)(sizeof(buf) - 1 - pos)));
}

/* ------------------------------------------------------------------ */
/* parse_int(s, base=10) → int  (throws on failure)                   */
/* ------------------------------------------------------------------ */

static bool try_parse_int_impl(const char* s, int base, ms_i64* result) {
    if (!s || *s == '\0') return false;
    char* end;
    errno = 0;
    long long v = strtoll(s, &end, base);
    if (end == s || *end != '\0' || errno == ERANGE) return false;
    *result = (ms_i64)v;
    return true;
}

static MsValue sc_parse_int(MsVM* vm, int argc, MsValue* argv) {
    MsObjString* s;
    if (!req_str(vm, argv[0], "strconv.parse_int", &s)) return MS_NIL_VAL();
    int base = 10;
    if (argc >= 2 && MS_IS_INT(argv[1])) base = (int)MS_AS_INT(argv[1]);

    ms_i64 result;
    if (!try_parse_int_impl(s->data, base, &result)) {
        ms_vm_runtime_error(vm, "strconv.parse_int: invalid integer \"%s\" (base %d)",
                            s->data, base);
        return MS_NIL_VAL();
    }
    return MS_INT_VAL(result);
}

/* ------------------------------------------------------------------ */
/* try_parse_int(s, base=10) → int|nil                                 */
/* ------------------------------------------------------------------ */

static MsValue sc_try_parse_int(MsVM* vm, int argc, MsValue* argv) {
    MsObjString* s;
    if (!req_str(vm, argv[0], "strconv.try_parse_int", &s)) return MS_NIL_VAL();
    int base = 10;
    if (argc >= 2 && MS_IS_INT(argv[1])) base = (int)MS_AS_INT(argv[1]);

    ms_i64 result;
    if (!try_parse_int_impl(s->data, base, &result)) return MS_NIL_VAL();
    return MS_INT_VAL(result);
}

/* ------------------------------------------------------------------ */
/* format_float(x, fmt='g', prec=-1) → str                            */
/* ------------------------------------------------------------------ */

static MsValue sc_format_float(MsVM* vm, int argc, MsValue* argv) {
    double x;
    if (MS_IS_NUMBER(argv[0]))      x = MS_AS_NUMBER(argv[0]);
    else if (MS_IS_INT(argv[0]))    x = (double)MS_AS_INT(argv[0]);
    else {
        ms_vm_runtime_error(vm, "strconv.format_float: expected number");
        return MS_NIL_VAL();
    }

    char fmt_ch = 'g';
    if (argc >= 2 && MS_IS_STRING(argv[1]) && MS_AS_STRING(argv[1])->length > 0) {
        char c = MS_AS_STRING(argv[1])->data[0];
        if (c == 'e' || c == 'E' || c == 'f' || c == 'F' || c == 'g' || c == 'G')
            fmt_ch = c;
        else {
            ms_vm_runtime_error(vm, "strconv.format_float: fmt must be 'e','f','g'");
            return MS_NIL_VAL();
        }
    }

    int prec = -1;
    if (argc >= 3 && MS_IS_INT(argv[2])) prec = (int)MS_AS_INT(argv[2]);

    char spec[8];
    if (prec < 0)
        snprintf(spec, sizeof(spec), "%%%c", fmt_ch);
    else
        snprintf(spec, sizeof(spec), "%%.%d%c", prec, fmt_ch);

    char buf[64];
    int n = snprintf(buf, sizeof(buf), spec, x);
    if (n < 0 || n >= (int)sizeof(buf)) {
        /* Fallback for very long representations */
        char* big = (char*)malloc((size_t)n + 2);
        if (!big) abort();
        snprintf(big, (size_t)n + 1, spec, x);
        MsValue v = MS_OBJ_VAL(ms_obj_string_copy(vm, big, n));
        free(big);
        return v;
    }
    return MS_OBJ_VAL(ms_obj_string_copy(vm, buf, n));
}

/* ------------------------------------------------------------------ */
/* parse_float(s) → num  (throws on failure)                          */
/* ------------------------------------------------------------------ */

static bool try_parse_float_impl(const char* s, double* result) {
    if (!s || *s == '\0') return false;
    char* end;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE) return false;
    *result = v;
    return true;
}

static MsValue sc_parse_float(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!req_str(vm, argv[0], "strconv.parse_float", &s)) return MS_NIL_VAL();

    double result;
    if (!try_parse_float_impl(s->data, &result)) {
        ms_vm_runtime_error(vm, "strconv.parse_float: invalid float \"%s\"", s->data);
        return MS_NIL_VAL();
    }
    return MS_NUMBER_VAL(result);
}

/* ------------------------------------------------------------------ */
/* try_parse_float(s) → num|nil                                        */
/* ------------------------------------------------------------------ */

static MsValue sc_try_parse_float(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!req_str(vm, argv[0], "strconv.try_parse_float", &s)) return MS_NIL_VAL();

    double result;
    if (!try_parse_float_impl(s->data, &result)) return MS_NIL_VAL();
    return MS_NUMBER_VAL(result);
}

/* ------------------------------------------------------------------ */
/* parse_bool(s) → bool                                                */
/* ------------------------------------------------------------------ */

static MsValue sc_parse_bool(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!req_str(vm, argv[0], "strconv.parse_bool", &s)) return MS_NIL_VAL();

    const char* d = s->data;
    if (strcmp(d, "true") == 0 || strcmp(d, "1") == 0 || strcmp(d, "yes") == 0)
        return MS_BOOL_VAL(true);
    if (strcmp(d, "false") == 0 || strcmp(d, "0") == 0 || strcmp(d, "no") == 0)
        return MS_BOOL_VAL(false);

    ms_vm_runtime_error(vm, "strconv.parse_bool: invalid bool \"%s\"", d);
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* format_bool(b) → str                                                */
/* ------------------------------------------------------------------ */

static MsValue sc_format_bool(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BOOL(argv[0])) {
        ms_vm_runtime_error(vm, "strconv.format_bool: expected bool");
        return MS_NIL_VAL();
    }
    const char* s = MS_AS_BOOL(argv[0]) ? "true" : "false";
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s, (int)strlen(s)));
}

/* ------------------------------------------------------------------ */
/* quote(s) → str   (add double-quotes + escape interior)             */
/* ------------------------------------------------------------------ */

static MsValue sc_quote(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!req_str(vm, argv[0], "strconv.quote", &s)) return MS_NIL_VAL();

    /* Worst case: every char expands to \xHH (4 chars) + 2 quotes */
    size_t cap = (size_t)s->length * 4 + 3;
    char* buf = (char*)malloc(cap);
    if (!buf) abort();

    int w = 0;
    buf[w++] = '"';
    for (int i = 0; i < s->length; i++) {
        char esc[4];
        /* For quote, escape '"' and '\' and control chars */
        char c = s->data[i];
        int n = escape_char(c, esc);
        /* Don't escape '\'' inside double-quoted strings */
        if (n == 2 && esc[0] == '\\' && esc[1] == '\'') {
            buf[w++] = '\'';
        } else {
            memcpy(buf + w, esc, (size_t)n);
            w += n;
        }
    }
    buf[w++] = '"';
    buf[w] = '\0';

    MsValue v = MS_OBJ_VAL(ms_obj_string_copy(vm, buf, w));
    free(buf);
    return v;
}

/* ------------------------------------------------------------------ */
/* unquote(s) → str   (strip outer quotes + unescape)                 */
/* ------------------------------------------------------------------ */

static MsValue sc_unquote(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!req_str(vm, argv[0], "strconv.unquote", &s)) return MS_NIL_VAL();

    if (s->length < 2 || s->data[0] != '"' || s->data[s->length - 1] != '"') {
        ms_vm_runtime_error(vm, "strconv.unquote: string must be surrounded by double quotes");
        return MS_NIL_VAL();
    }

    const char* inner = s->data + 1;
    int inner_len = s->length - 2;
    char* buf = (char*)malloc((size_t)inner_len + 1);
    if (!buf) abort();

    int w = 0, r = 0;
    while (r < inner_len) {
        char c;
        if (inner[r] == '\\') {
            if (r + 1 >= inner_len) {
                free(buf);
                ms_vm_runtime_error(vm, "strconv.unquote: invalid escape at end");
                return MS_NIL_VAL();
            }
            int consumed = unescape_one(inner + r, &c);
            if (consumed == 0) {
                free(buf);
                ms_vm_runtime_error(vm, "strconv.unquote: unknown escape '\\%c'",
                                    inner[r + 1]);
                return MS_NIL_VAL();
            }
            r += consumed;
        } else {
            c = inner[r++];
        }
        buf[w++] = c;
    }
    buf[w] = '\0';

    MsValue v = MS_OBJ_VAL(ms_obj_string_copy(vm, buf, w));
    free(buf);
    return v;
}

/* ------------------------------------------------------------------ */
/* quote_rune(c) → str   (single char → 'c' with escape)             */
/* ------------------------------------------------------------------ */

static MsValue sc_quote_rune(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!req_str(vm, argv[0], "strconv.quote_rune", &s)) return MS_NIL_VAL();
    if (s->length == 0) {
        ms_vm_runtime_error(vm, "strconv.quote_rune: empty string");
        return MS_NIL_VAL();
    }

    char c = s->data[0];
    char esc[4];
    /* Don't escape '"' inside single-quoted rune */
    int n;
    if (c == '"') { esc[0] = '"'; n = 1; }
    else if (c == '\'') { esc[0]='\\'; esc[1]='\''; n = 2; }
    else n = escape_char(c, esc);

    char buf[8];
    buf[0] = '\'';
    memcpy(buf + 1, esc, (size_t)n);
    buf[1 + n] = '\'';
    buf[2 + n] = '\0';

    return MS_OBJ_VAL(ms_obj_string_copy(vm, buf, 2 + n));
}

/* ------------------------------------------------------------------ */
/* Registration                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_strconv_defs[] = {
    /* integer */
    {"format_int",      sc_format_int,      -1},
    {"parse_int",       sc_parse_int,       -1},
    {"try_parse_int",   sc_try_parse_int,   -1},
    /* float */
    {"format_float",    sc_format_float,    -1},
    {"parse_float",     sc_parse_float,      1},
    {"try_parse_float", sc_try_parse_float,  1},
    /* bool */
    {"parse_bool",      sc_parse_bool,       1},
    {"format_bool",     sc_format_bool,      1},
    /* quote */
    {"quote",           sc_quote,            1},
    {"unquote",         sc_unquote,          1},
    {"quote_rune",      sc_quote_rune,       1},
    {NULL, NULL, 0}
};

void ms_module_strconv_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_strconv_defs);
}
