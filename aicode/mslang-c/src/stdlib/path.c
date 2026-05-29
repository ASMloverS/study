/* src/stdlib/path.c - STDLIB-29: path module
 *
 * Path string utilities: join, split, dirname, basename, ext, stem,
 * normalize, absolute, relative, is_abs, is_rel, parts, split_ext.
 * Uses OS-native separator; no filesystem calls (except absolute).
 *
 * Public API:
 *   path.dirname(p)           → str
 *   path.basename(p)          → str
 *   path.ext(p)               → str
 *   path.stem(p)              → str
 *   path.split(p)             → [str, str]
 *   path.split_ext(p)         → [str, str]
 *   path.parts(p)             → list[str]
 *   path.join(part1, ...)     → str
 *   path.normalize(p)         → str
 *   path.absolute(p)          → str
 *   path.relative(p, base)    → str
 *   path.is_abs(p)            → bool
 *   path.is_rel(p)            → bool
 *   path.sep                  → str constant
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define PATH_SEP '\\'
#else
#  include <unistd.h>
#  include <limits.h>
#  define PATH_SEP '/'
#endif

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static MsValue make_str(MsVM* vm, const char* s) {
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s, (int)strlen(s)));
}

static MsValue make_str_n(MsVM* vm, const char* s, int n) {
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s, n));
}

static bool is_sep(char c) {
    return c == '/' || c == '\\';
}

/* Return true if p is an absolute path (handles Unix / and Windows C:\, \\ ) */
static bool path_is_absolute(const char* p) {
    if (!p || !p[0]) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
#ifdef _WIN32
    /* C:\ or C:/ */
    if (p[0] && p[1] == ':' && (p[2] == '/' || p[2] == '\\')) return true;
#endif
    return false;
}

/* Find the position of the last separator, or -1 if none.
   Ignores trailing separators so "foo/bar/" → finds the one before "bar/". */
static int last_sep_pos(const char* p) {
    int len = (int)strlen(p);
    /* strip trailing seps */
    int end = len - 1;
    while (end > 0 && is_sep(p[end])) end--;
    for (int i = end; i >= 0; i--) {
        if (is_sep(p[i])) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* path.dirname(p) → str                                               */
/* ------------------------------------------------------------------ */
static MsValue path_dirname(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.dirname: expected string");
        return MS_NIL_VAL();
    }
    const char* p = MS_AS_CSTRING(argv[0]);
    int pos = last_sep_pos(p);
    if (pos < 0)  return make_str(vm, ".");
    if (pos == 0) return make_str(vm, "/");
    return make_str_n(vm, p, pos);
}

/* ------------------------------------------------------------------ */
/* path.basename(p) → str                                              */
/* ------------------------------------------------------------------ */
static MsValue path_basename(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.basename: expected string");
        return MS_NIL_VAL();
    }
    const char* p = MS_AS_CSTRING(argv[0]);
    /* strip trailing seps */
    int len = (int)strlen(p);
    int end = len - 1;
    while (end > 0 && is_sep(p[end])) end--;
    /* find last sep */
    int start = 0;
    for (int i = end; i >= 0; i--) {
        if (is_sep(p[i])) { start = i + 1; break; }
    }
    return make_str_n(vm, p + start, end - start + 1);
}

/* ------------------------------------------------------------------ */
/* path.ext(p) → str (includes the dot, or "" if none)                */
/* ------------------------------------------------------------------ */
static MsValue path_ext(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.ext: expected string");
        return MS_NIL_VAL();
    }
    const char* p = MS_AS_CSTRING(argv[0]);
    /* search for last dot after last separator */
    const char* last_dot = NULL;
    for (const char* q = p; *q; q++) {
        if (is_sep(*q)) last_dot = NULL;
        else if (*q == '.') last_dot = q;
    }
    if (!last_dot || last_dot == p) return make_str(vm, "");
    return make_str(vm, last_dot);
}

/* ------------------------------------------------------------------ */
/* path.stem(p) → str (basename without extension)                     */
/* ------------------------------------------------------------------ */
static MsValue path_stem(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.stem: expected string");
        return MS_NIL_VAL();
    }
    const char* p = MS_AS_CSTRING(argv[0]);
    /* find start of basename */
    int len = (int)strlen(p);
    int end = len - 1;
    while (end > 0 && is_sep(p[end])) end--;
    int start = 0;
    for (int i = end; i >= 0; i--) {
        if (is_sep(p[i])) { start = i + 1; break; }
    }
    /* find last dot in the basename */
    const char* base = p + start;
    const char* last_dot = NULL;
    for (const char* q = base; q <= p + end; q++) {
        if (*q == '.') last_dot = q;
    }
    if (!last_dot || last_dot == base)
        return make_str_n(vm, base, end - start + 1);
    return make_str_n(vm, base, (int)(last_dot - base));
}

/* ------------------------------------------------------------------ */
/* path.split(p) → [dirname, basename]                                 */
/* ------------------------------------------------------------------ */
static MsValue path_split(MsVM* vm, int argc, MsValue* argv) {
    MsValue d = path_dirname(vm, argc, argv);
    MsValue b = path_basename(vm, argc, argv);
    MsValue items[2] = {d, b};
    return MS_OBJ_VAL(ms_obj_list_from_array(vm, items, 2));
}

/* ------------------------------------------------------------------ */
/* path.split_ext(p) → [root, ext]                                     */
/* ------------------------------------------------------------------ */
static MsValue path_split_ext(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.split_ext: expected string");
        return MS_NIL_VAL();
    }
    const char* p = MS_AS_CSTRING(argv[0]);
    const char* last_dot = NULL;
    for (const char* q = p; *q; q++) {
        if (is_sep(*q)) last_dot = NULL;
        else if (*q == '.') last_dot = q;
    }
    MsValue root, ext;
    if (!last_dot || last_dot == p) {
        root = make_str(vm, p);
        ext  = make_str(vm, "");
    } else {
        root = make_str_n(vm, p, (int)(last_dot - p));
        ext  = make_str(vm, last_dot);
    }
    MsValue items[2] = {root, ext};
    return MS_OBJ_VAL(ms_obj_list_from_array(vm, items, 2));
}

/* ------------------------------------------------------------------ */
/* path.parts(p) → list[str]  (splits on separators, drops empties)   */
/* ------------------------------------------------------------------ */
static MsValue path_parts(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.parts: expected string");
        return MS_NIL_VAL();
    }
    const char* p = MS_AS_CSTRING(argv[0]);
    MsObjList* lst = ms_obj_list_new(vm);
    const char* seg = p;
    while (*seg) {
        /* skip separators */
        while (*seg && is_sep(*seg)) seg++;
        if (!*seg) break;
        const char* end = seg;
        while (*end && !is_sep(*end)) end++;
        MsValue s = make_str_n(vm, seg, (int)(end - seg));
        ms_value_array_push(&lst->items, s);
        seg = end;
    }
    return MS_OBJ_VAL(lst);
}

/* ------------------------------------------------------------------ */
/* path.join(part1, part2, ...) → str                                  */
/* ------------------------------------------------------------------ */
static MsValue path_join(MsVM* vm, int argc, MsValue* argv) {
    if (argc == 0) return make_str(vm, "");
    for (int i = 0; i < argc; i++) {
        if (!MS_IS_STRING(argv[i])) {
            ms_vm_runtime_error(vm, "path.join: all arguments must be strings");
            return MS_NIL_VAL();
        }
    }
    size_t cap = 256;
    char* buf = (char*)malloc(cap);
    if (!buf) { ms_vm_runtime_error(vm, "path.join: OOM"); return MS_NIL_VAL(); }
    size_t pos = 0;

    for (int i = 0; i < argc; i++) {
        const char* part = MS_AS_CSTRING(argv[i]);
        size_t plen = strlen(part);
        if (path_is_absolute(part)) pos = 0; /* absolute part resets */
        /* add separator if needed */
        if (pos > 0 && !is_sep(buf[pos - 1])) {
            if (pos + 1 >= cap) {
                cap *= 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) { free(buf); ms_vm_runtime_error(vm, "path.join: OOM"); return MS_NIL_VAL(); }
                buf = nb;
            }
            buf[pos++] = PATH_SEP;
        }
        while (pos + plen + 1 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); ms_vm_runtime_error(vm, "path.join: OOM"); return MS_NIL_VAL(); }
            buf = nb;
        }
        memcpy(buf + pos, part, plen);
        pos += plen;
    }
    buf[pos] = '\0';
    MsValue result = make_str(vm, buf);
    free(buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* path.normalize(p) → str  (resolve ./ and ../, collapse // )        */
/* ------------------------------------------------------------------ */
static MsValue path_normalize(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.normalize: expected string");
        return MS_NIL_VAL();
    }
    const char* p = MS_AS_CSTRING(argv[0]);
    int len = (int)strlen(p);
    if (len == 0) return make_str(vm, ".");

    bool abs = path_is_absolute(p);

    /* Split into components using a temp copy */
    char* tmp = (char*)malloc((size_t)(len + 1));
    if (!tmp) { ms_vm_runtime_error(vm, "path.normalize: OOM"); return MS_NIL_VAL(); }
    memcpy(tmp, p, (size_t)(len + 1));

    /* Stack of component start/len pairs */
    typedef struct { const char* s; int n; } Seg;
    Seg* stk = (Seg*)malloc((size_t)(len + 1) * sizeof(Seg));
    if (!stk) { free(tmp); ms_vm_runtime_error(vm, "path.normalize: OOM"); return MS_NIL_VAL(); }
    int top = 0;

    char* q = tmp;
    while (*q) {
        while (*q && is_sep(*q)) q++;
        if (!*q) break;
        char* end = q;
        while (*end && !is_sep(*end)) end++;
        int slen = (int)(end - q);
        if (slen == 1 && q[0] == '.') {
            /* skip . */
        } else if (slen == 2 && q[0] == '.' && q[1] == '.') {
            if (top > 0 && !(stk[top-1].n == 2 &&
                              stk[top-1].s[0] == '.' &&
                              stk[top-1].s[1] == '.')) {
                top--;
            } else if (!abs) {
                stk[top++] = (Seg){q, slen};
            }
        } else {
            stk[top++] = (Seg){q, slen};
        }
        q = end;
    }

    /* Rebuild */
    size_t out_cap = (size_t)(len + 4);
    char* out = (char*)malloc(out_cap);
    if (!out) { free(tmp); free(stk); ms_vm_runtime_error(vm, "path.normalize: OOM"); return MS_NIL_VAL(); }
    size_t out_pos = 0;

    if (abs) {
        out[out_pos++] = '/';
    }
    for (int i = 0; i < top; i++) {
        if (i > 0) out[out_pos++] = PATH_SEP;
        size_t sl = (size_t)stk[i].n;
        memcpy(out + out_pos, stk[i].s, sl);
        out_pos += sl;
    }
    if (out_pos == 0) {
        out[out_pos++] = '.';
    }
    out[out_pos] = '\0';

    MsValue result = make_str(vm, out);
    free(tmp); free(stk); free(out);
    return result;
}

/* ------------------------------------------------------------------ */
/* path.absolute(p) → str                                              */
/* ------------------------------------------------------------------ */
static MsValue path_absolute(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.absolute: expected string");
        return MS_NIL_VAL();
    }
    const char* p = MS_AS_CSTRING(argv[0]);
    if (path_is_absolute(p)) return make_str(vm, p);

    /* Prepend cwd */
#ifdef _WIN32
    char cwd[MAX_PATH];
    if (!GetCurrentDirectoryA(MAX_PATH, cwd)) {
        ms_vm_runtime_error(vm, "path.absolute: failed to get cwd");
        return MS_NIL_VAL();
    }
#else
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        ms_vm_runtime_error(vm, "path.absolute: failed to get cwd");
        return MS_NIL_VAL();
    }
#endif
    /* join cwd + p then normalize */
    size_t clen = strlen(cwd);
    size_t plen = strlen(p);
    size_t tlen = clen + 1 + plen + 1;
    char* joined = (char*)malloc(tlen);
    if (!joined) { ms_vm_runtime_error(vm, "path.absolute: OOM"); return MS_NIL_VAL(); }
    memcpy(joined, cwd, clen);
    joined[clen] = PATH_SEP;
    memcpy(joined + clen + 1, p, plen + 1);

    MsValue jv = make_str(vm, joined);
    free(joined);
    MsValue a1 = jv;
    return path_normalize(vm, 1, &a1);
}

/* ------------------------------------------------------------------ */
/* path.relative(p, base) → str                                        */
/* ------------------------------------------------------------------ */
static MsValue path_relative(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "path.relative: expected strings");
        return MS_NIL_VAL();
    }
    /* Normalize both first */
    MsValue nv_p    = path_normalize(vm, 1, &argv[0]);
    MsValue nv_base = path_normalize(vm, 1, &argv[1]);
    const char* pp = MS_AS_CSTRING(nv_p);
    const char* bp = MS_AS_CSTRING(nv_base);

    /* split into component arrays */
    int plen = (int)strlen(pp);
    int blen = (int)strlen(bp);
    char* ptmp = (char*)malloc((size_t)(plen + 1));
    char* btmp = (char*)malloc((size_t)(blen + 1));
    if (!ptmp || !btmp) {
        free(ptmp); free(btmp);
        ms_vm_runtime_error(vm, "path.relative: OOM");
        return MS_NIL_VAL();
    }
    memcpy(ptmp, pp, (size_t)(plen + 1));
    memcpy(btmp, bp, (size_t)(blen + 1));

    /* Tokenize into arrays (max 256 parts each) */
    const char* pparts[256]; int pn = 0;
    const char* bparts[256]; int bn = 0;
    char* t;

    t = ptmp;
    while (*t) {
        while (*t && is_sep(*t)) t++;
        if (!*t) break;
        char* e = t;
        while (*e && !is_sep(*e)) e++;
        if (pn < 256) pparts[pn++] = t;
        if (*e) { *e = '\0'; t = e + 1; } else { t = e; }
    }
    t = btmp;
    while (*t) {
        while (*t && is_sep(*t)) t++;
        if (!*t) break;
        char* e = t;
        while (*e && !is_sep(*e)) e++;
        if (bn < 256) bparts[bn++] = t;
        if (*e) { *e = '\0'; t = e + 1; } else { t = e; }
    }

    /* find common prefix length */
    int common = 0;
    int min_n  = pn < bn ? pn : bn;
    while (common < min_n && strcmp(pparts[common], bparts[common]) == 0)
        common++;

    /* number of ".." needed = bn - common */
    int ups = bn - common;
    int rest = pn - common;

    /* build result */
    size_t out_cap = (size_t)(ups * 3 + rest * 64 + 4);
    char* out = (char*)malloc(out_cap);
    if (!out) { free(ptmp); free(btmp); ms_vm_runtime_error(vm, "path.relative: OOM"); return MS_NIL_VAL(); }
    size_t out_pos = 0;

    for (int i = 0; i < ups; i++) {
        if (out_pos > 0) out[out_pos++] = PATH_SEP;
        out[out_pos++] = '.';
        out[out_pos++] = '.';
    }
    for (int i = common; i < pn; i++) {
        if (out_pos > 0) out[out_pos++] = PATH_SEP;
        size_t sl = strlen(pparts[i]);
        /* grow if needed */
        if (out_pos + sl + 2 >= out_cap) {
            out_cap = out_pos + sl + 64;
            char* nb = (char*)realloc(out, out_cap);
            if (!nb) { free(out); free(ptmp); free(btmp); ms_vm_runtime_error(vm, "path.relative: OOM"); return MS_NIL_VAL(); }
            out = nb;
        }
        memcpy(out + out_pos, pparts[i], sl);
        out_pos += sl;
    }
    if (out_pos == 0) {
        out[out_pos++] = '.';
    }
    out[out_pos] = '\0';

    MsValue result = make_str(vm, out);
    free(out); free(ptmp); free(btmp);
    return result;
}

/* ------------------------------------------------------------------ */
/* path.is_abs(p) → bool                                               */
/* ------------------------------------------------------------------ */
static MsValue path_is_abs(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.is_abs: expected string");
        return MS_NIL_VAL();
    }
    return MS_BOOL_VAL(path_is_absolute(MS_AS_CSTRING(argv[0])));
}

/* ------------------------------------------------------------------ */
/* path.is_rel(p) → bool                                               */
/* ------------------------------------------------------------------ */
static MsValue path_is_rel(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "path.is_rel: expected string");
        return MS_NIL_VAL();
    }
    return MS_BOOL_VAL(!path_is_absolute(MS_AS_CSTRING(argv[0])));
}

/* ================================================================== */
/* Module init                                                          */
/* ================================================================== */

void ms_module_path_init(MsVM* vm, MsObjModule* mod) {
    static const MsNativeDef defs[] = {
        {"dirname",   path_dirname,   1},
        {"basename",  path_basename,  1},
        {"ext",       path_ext,       1},
        {"stem",      path_stem,      1},
        {"split",     path_split,     1},
        {"split_ext", path_split_ext, 1},
        {"parts",     path_parts,     1},
        {"join",      path_join,     -1},
        {"normalize", path_normalize, 1},
        {"absolute",  path_absolute,  1},
        {"relative",  path_relative,  2},
        {"is_abs",    path_is_abs,    1},
        {"is_rel",    path_is_rel,    1},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);

#ifdef _WIN32
    ms_module_export_value(vm, mod, "sep", make_str(vm, "\\"));
#else
    ms_module_export_value(vm, mod, "sep", make_str(vm, "/"));
#endif
}
