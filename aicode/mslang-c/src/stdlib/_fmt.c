/* src/stdlib/_fmt.c - STDLIB-15: fmt module (C primitive layer)
 *
 * sprintf(format, ...) → str
 * printf(format, ...)  → nil   (stdout)
 * println(format, ...) → nil   (stdout + \n)
 * eprintf(format, ...) → nil   (stderr)
 * format(value)        → str   (<type: value>)
 *
 * Supports: %d %i %u %x %X %o %b %f %e %E %g %G %s %q %v %c %%
 * Modifiers: - + 0  width  .prec
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/* Dynamic string buffer                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} FmtBuf;

static void fb_init(FmtBuf* b) {
    b->cap  = 64;
    b->len  = 0;
    b->data = (char*)malloc(b->cap);
    if (!b->data) abort();
}

static void fb_free(FmtBuf* b) { free(b->data); }

static void fb_reserve(FmtBuf* b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    while (b->len + extra > b->cap) b->cap *= 2;
    b->data = (char*)realloc(b->data, b->cap);
    if (!b->data) abort();
}

static void fb_append(FmtBuf* b, const char* s, size_t n) {
    fb_reserve(b, n + 1);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void fb_appendc(FmtBuf* b, char c) { fb_append(b, &c, 1); }

/* ------------------------------------------------------------------ */
/* quote helper (mirrors strconv.quote without calling back into VM)   */
/* ------------------------------------------------------------------ */

static void escape_char_to_buf(FmtBuf* b, char c) {
    switch (c) {
        case '\n': fb_append(b, "\\n",  2); break;
        case '\t': fb_append(b, "\\t",  2); break;
        case '\r': fb_append(b, "\\r",  2); break;
        case '\\': fb_append(b, "\\\\", 2); break;
        case '"':  fb_append(b, "\\\"", 2); break;
        default:
            if ((unsigned char)c < 0x20) {
                char esc[5];
                static const char hex[] = "0123456789abcdef";
                esc[0]='\\'; esc[1]='x';
                esc[2]=hex[((unsigned char)c)>>4];
                esc[3]=hex[((unsigned char)c)&0xF];
                esc[4]='\0';
                fb_append(b, esc, 4);
            } else {
                fb_appendc(b, c);
            }
            break;
    }
}

static void fb_quote_string(FmtBuf* b, const char* s, int len) {
    fb_appendc(b, '"');
    for (int i = 0; i < len; i++) escape_char_to_buf(b, s[i]);
    fb_appendc(b, '"');
}

/* ------------------------------------------------------------------ */
/* Integer → string helpers                                            */
/* ------------------------------------------------------------------ */

/* Write integer n in given base (2..36) into buf[64].
   Returns pointer to start within buf, sets *out_len. */
static const char* fmt_uint64(uint64_t n, int base, bool upper,
                               char buf[72], int* out_len) {
    static const char lo[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static const char hi[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char* digits = upper ? hi : lo;
    int pos = 71;
    buf[pos] = '\0';
    if (n == 0) { buf[--pos] = '0'; }
    else {
        while (n > 0) {
            buf[--pos] = digits[(int)(n % (uint64_t)base)];
            n /= (uint64_t)base;
        }
    }
    *out_len = 71 - pos;
    return buf + pos;
}

/* ------------------------------------------------------------------ */
/* Apply width/padding to a formatted segment and append to FmtBuf    */
/* ------------------------------------------------------------------ */

static void fb_pad(FmtBuf* b, const char* s, int slen,
                   int width, bool left_align, char pad_ch,
                   const char* prefix, int pfx_len) {
    int content = pfx_len + slen;
    int pad = (width > content) ? (width - content) : 0;

    if (left_align) {
        fb_append(b, prefix, (size_t)pfx_len);
        fb_append(b, s, (size_t)slen);
        for (int i = 0; i < pad; i++) fb_appendc(b, ' ');
    } else if (pad_ch == '0' && pfx_len > 0) {
        /* sign/prefix before zeros */
        fb_append(b, prefix, (size_t)pfx_len);
        for (int i = 0; i < pad; i++) fb_appendc(b, '0');
        fb_append(b, s, (size_t)slen);
    } else {
        for (int i = 0; i < pad; i++) fb_appendc(b, pad_ch);
        fb_append(b, prefix, (size_t)pfx_len);
        fb_append(b, s, (size_t)slen);
    }
}

/* ------------------------------------------------------------------ */
/* Core format engine                                                  */
/* ------------------------------------------------------------------ */

/* Returns true on success, false on error (sets *err_msg). */
static bool fmt_do(FmtBuf* out, const char* fmt, int argc, MsValue* argv,
                   const char** err_msg) {
    int arg_idx = 0;
    const char* p = fmt;

    while (*p) {
        if (*p != '%') { fb_appendc(out, *p++); continue; }
        p++; /* skip '%' */

        /* %% */
        if (*p == '%') { fb_appendc(out, '%'); p++; continue; }

        /* Parse flags */
        bool flag_left  = false;
        bool flag_plus  = false;
        bool flag_zero  = false;
        while (*p == '-' || *p == '+' || *p == '0' || *p == ' ') {
            if (*p == '-') flag_left = true;
            else if (*p == '+') flag_plus = true;
            else if (*p == '0') flag_zero = true;
            p++;
        }

        /* Parse width */
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        /* Parse precision */
        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            while (*p >= '0' && *p <= '9') {
                prec = prec * 10 + (*p - '0');
                p++;
            }
        }

        char spec = *p++;
        if (!spec) { *err_msg = "fmt: truncated format specifier"; return false; }

        /* Consume one argument */
        if (arg_idx >= argc) {
            *err_msg = "fmt.sprintf: not enough arguments";
            return false;
        }
        MsValue val = argv[arg_idx++];

        char pad_ch = (flag_zero && !flag_left) ? '0' : ' ';

        switch (spec) {
            /* Integer specifiers */
            case 'd': case 'i': {
                int64_t n = 0;
                if (MS_IS_INT(val)) n = (int64_t)MS_AS_INT(val);
                else if (MS_IS_NUMBER(val)) n = (int64_t)MS_AS_NUMBER(val);
                else { *err_msg = "fmt: %d expects int"; return false; }

                char buf[72];
                int  len;
                uint64_t un = (n < 0) ? (uint64_t)(-(n + 1)) + 1ULL : (uint64_t)n;
                const char* digits = fmt_uint64(un, 10, false, buf, &len);

                char prefix[2] = {0, 0};
                int  pfxlen = 0;
                if (n < 0) { prefix[0] = '-'; pfxlen = 1; }
                else if (flag_plus) { prefix[0] = '+'; pfxlen = 1; }

                fb_pad(out, digits, len, width, flag_left, pad_ch, prefix, pfxlen);
                break;
            }
            case 'u': {
                uint64_t n = 0;
                if (MS_IS_INT(val)) n = (uint64_t)MS_AS_INT(val);
                else if (MS_IS_NUMBER(val)) n = (uint64_t)(int64_t)MS_AS_NUMBER(val);
                else { *err_msg = "fmt: %u expects int"; return false; }

                char buf[72]; int len;
                const char* digits = fmt_uint64(n, 10, false, buf, &len);
                fb_pad(out, digits, len, width, flag_left, pad_ch, "", 0);
                break;
            }
            case 'x': case 'X': {
                uint64_t n = 0;
                if (MS_IS_INT(val)) n = (uint64_t)MS_AS_INT(val);
                else if (MS_IS_NUMBER(val)) n = (uint64_t)(int64_t)MS_AS_NUMBER(val);
                else { *err_msg = "fmt: %x expects int"; return false; }

                char buf[72]; int len;
                const char* digits = fmt_uint64(n, 16, (spec == 'X'), buf, &len);
                fb_pad(out, digits, len, width, flag_left, pad_ch, "", 0);
                break;
            }
            case 'o': {
                uint64_t n = 0;
                if (MS_IS_INT(val)) n = (uint64_t)MS_AS_INT(val);
                else if (MS_IS_NUMBER(val)) n = (uint64_t)(int64_t)MS_AS_NUMBER(val);
                else { *err_msg = "fmt: %o expects int"; return false; }

                char buf[72]; int len;
                const char* digits = fmt_uint64(n, 8, false, buf, &len);
                fb_pad(out, digits, len, width, flag_left, pad_ch, "", 0);
                break;
            }
            case 'b': {
                uint64_t n = 0;
                if (MS_IS_INT(val)) n = (uint64_t)MS_AS_INT(val);
                else if (MS_IS_NUMBER(val)) n = (uint64_t)(int64_t)MS_AS_NUMBER(val);
                else { *err_msg = "fmt: %b expects int"; return false; }

                char buf[72]; int len;
                const char* digits = fmt_uint64(n, 2, false, buf, &len);
                fb_pad(out, digits, len, width, flag_left, pad_ch, "", 0);
                break;
            }
            /* Float specifiers */
            case 'f': case 'F':
            case 'e': case 'E':
            case 'g': case 'G': {
                double x = 0.0;
                if (MS_IS_NUMBER(val)) x = MS_AS_NUMBER(val);
                else if (MS_IS_INT(val)) x = (double)MS_AS_INT(val);
                else { *err_msg = "fmt: float specifier expects number"; return false; }

                char fmtbuf[16];
                int fi = 0;
                fmtbuf[fi++] = '%';
                if (flag_plus) fmtbuf[fi++] = '+';
                if (flag_left) fmtbuf[fi++] = '-';
                if (flag_zero) fmtbuf[fi++] = '0';
                if (width > 0) {
                    fi += snprintf(fmtbuf + fi, sizeof(fmtbuf) - (size_t)fi,
                                   "%d", width);
                }
                if (prec >= 0) {
                    fi += snprintf(fmtbuf + fi, sizeof(fmtbuf) - (size_t)fi,
                                   ".%d", prec);
                }
                fmtbuf[fi++] = spec;
                fmtbuf[fi]   = '\0';

                char tmp[64];
                int n = snprintf(tmp, sizeof(tmp), fmtbuf, x);
                if (n < 0) { *err_msg = "fmt: float format error"; return false; }
                if (n < (int)sizeof(tmp)) {
                    fb_append(out, tmp, (size_t)n);
                } else {
                    char* big = (char*)malloc((size_t)n + 2);
                    if (!big) abort();
                    snprintf(big, (size_t)n + 1, fmtbuf, x);
                    fb_append(out, big, (size_t)n);
                    free(big);
                }
                break;
            }
            /* String */
            case 's': {
                const char* s;
                int slen;
                char* tmp_cs = NULL;
                if (MS_IS_STRING(val)) {
                    MsObjString* so = MS_AS_STRING(val);
                    s = so->data; slen = so->length;
                } else {
                    tmp_cs = ms_value_to_cstring(val);
                    s = tmp_cs; slen = (int)strlen(s);
                }
                if (prec >= 0 && prec < slen) slen = prec;
                fb_pad(out, s, slen, width, flag_left, ' ', "", 0);
                free(tmp_cs);
                break;
            }
            /* Quoted string */
            case 'q': {
                if (!MS_IS_STRING(val)) {
                    *err_msg = "fmt: %q expects string";
                    return false;
                }
                MsObjString* so = MS_AS_STRING(val);
                /* Build quoted string into a temp FmtBuf then pad */
                FmtBuf qb;
                fb_init(&qb);
                fb_quote_string(&qb, so->data, so->length);
                fb_pad(out, qb.data, (int)qb.len, width, flag_left, ' ', "", 0);
                fb_free(&qb);
                break;
            }
            /* Default / any-type */
            case 'v': {
                char* cs = ms_value_to_cstring(val);
                int slen = (int)strlen(cs);
                fb_pad(out, cs, slen, width, flag_left, ' ', "", 0);
                free(cs);
                break;
            }
            /* Unicode code point → char */
            case 'c': {
                int64_t cp = 0;
                if (MS_IS_INT(val)) cp = (int64_t)MS_AS_INT(val);
                else if (MS_IS_NUMBER(val)) cp = (int64_t)MS_AS_NUMBER(val);
                else { *err_msg = "fmt: %c expects int"; return false; }
                /* Simple ASCII / latin-1 for now */
                char ch = (char)(cp & 0xFF);
                fb_pad(out, &ch, 1, width, flag_left, ' ', "", 0);
                break;
            }
            default: {
                *err_msg = "fmt.sprintf: unknown format specifier";
                return false;
            }
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* sprintf(format, ...) → str                                          */
/* ------------------------------------------------------------------ */

static MsValue fmt_sprintf(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "fmt.sprintf: first argument must be a format string");
        return MS_NIL_VAL();
    }
    MsObjString* fmt_str = MS_AS_STRING(argv[0]);
    FmtBuf buf;
    fb_init(&buf);
    const char* err = NULL;
    bool ok = fmt_do(&buf, fmt_str->data, argc - 1, argv + 1, &err);
    if (!ok) {
        fb_free(&buf);
        ms_vm_runtime_error(vm, "%s", err ? err : "fmt.sprintf: format error");
        return MS_NIL_VAL();
    }
    MsValue result = MS_OBJ_VAL(ms_obj_string_copy(vm, buf.data, (int)buf.len));
    fb_free(&buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* printf(format, ...) → nil                                           */
/* ------------------------------------------------------------------ */

static MsValue fmt_printf(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "fmt.printf: first argument must be a format string");
        return MS_NIL_VAL();
    }
    MsObjString* fmt_str = MS_AS_STRING(argv[0]);
    FmtBuf buf;
    fb_init(&buf);
    const char* err = NULL;
    bool ok = fmt_do(&buf, fmt_str->data, argc - 1, argv + 1, &err);
    if (!ok) {
        fb_free(&buf);
        ms_vm_runtime_error(vm, "%s", err ? err : "fmt.printf: format error");
        return MS_NIL_VAL();
    }
    fwrite(buf.data, 1, buf.len, stdout);
    fb_free(&buf);
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* println(format, ...) → nil  (appends \n)                           */
/* ------------------------------------------------------------------ */

static MsValue fmt_println(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "fmt.println: first argument must be a format string");
        return MS_NIL_VAL();
    }
    MsObjString* fmt_str = MS_AS_STRING(argv[0]);
    FmtBuf buf;
    fb_init(&buf);
    const char* err = NULL;
    bool ok = fmt_do(&buf, fmt_str->data, argc - 1, argv + 1, &err);
    if (!ok) {
        fb_free(&buf);
        ms_vm_runtime_error(vm, "%s", err ? err : "fmt.println: format error");
        return MS_NIL_VAL();
    }
    fwrite(buf.data, 1, buf.len, stdout);
    fputc('\n', stdout);
    fb_free(&buf);
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* eprintf(format, ...) → nil                                          */
/* ------------------------------------------------------------------ */

static MsValue fmt_eprintf(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "fmt.eprintf: first argument must be a format string");
        return MS_NIL_VAL();
    }
    MsObjString* fmt_str = MS_AS_STRING(argv[0]);
    FmtBuf buf;
    fb_init(&buf);
    const char* err = NULL;
    bool ok = fmt_do(&buf, fmt_str->data, argc - 1, argv + 1, &err);
    if (!ok) {
        fb_free(&buf);
        ms_vm_runtime_error(vm, "%s", err ? err : "fmt.eprintf: format error");
        return MS_NIL_VAL();
    }
    fwrite(buf.data, 1, buf.len, stderr);
    fb_free(&buf);
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* format(value) → str  (<type: repr>)                                */
/* ------------------------------------------------------------------ */

static MsValue fmt_format(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsValue val = argv[0];
    FmtBuf buf;
    fb_init(&buf);

    if (MS_IS_NIL(val)) {
        fb_append(&buf, "<nil>", 5);
    } else if (MS_IS_BOOL(val)) {
        const char* bv = MS_AS_BOOL(val) ? "true" : "false";
        fb_append(&buf, "<bool: ", 7);
        fb_append(&buf, bv, (size_t)strlen(bv));
        fb_appendc(&buf, '>');
    } else if (MS_IS_INT(val)) {
        char tmp[32];
        int n = snprintf(tmp, sizeof(tmp), "<int: %" PRId64 ">",
                         (int64_t)MS_AS_INT(val));
        fb_append(&buf, tmp, (size_t)n);
    } else if (MS_IS_NUMBER(val)) {
        char tmp[64];
        int n = snprintf(tmp, sizeof(tmp), "<num: %g>", MS_AS_NUMBER(val));
        fb_append(&buf, tmp, (size_t)n);
    } else if (MS_IS_STRING(val)) {
        MsObjString* s = MS_AS_STRING(val);
        fb_append(&buf, "<string: ", 9);
        fb_append(&buf, s->data, (size_t)s->length);
        fb_appendc(&buf, '>');
    } else {
        char* cs = ms_value_to_cstring(val);
        fb_append(&buf, "<", 1);
        fb_append(&buf, cs, strlen(cs));
        fb_appendc(&buf, '>');
        free(cs);
    }

    MsValue result = MS_OBJ_VAL(ms_obj_string_copy(vm, buf.data, (int)buf.len));
    fb_free(&buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* Registration                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_fmt_defs[] = {
    {"sprintf", fmt_sprintf, -1},
    {"printf",  fmt_printf,  -1},
    {"println", fmt_println, -1},
    {"eprintf", fmt_eprintf, -1},
    {"format",  fmt_format,   1},
    {NULL, NULL, 0}
};

void ms_module_fmt_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_fmt_defs);
}
