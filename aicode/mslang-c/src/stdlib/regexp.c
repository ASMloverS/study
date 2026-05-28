/* src/stdlib/regexp.c - STDLIB-28: regexp module
 *
 * Public API:
 *   regexp.compile(pattern)              → Regex
 *   regexp.match(pattern, s)             → Match|nil
 *   regexp.search(pattern, s)            → Match|nil
 *   regexp.find_all(pattern, s)          → list[Match]
 *   regexp.replace(pattern, s, repl)     → str
 *   regexp.replace_n(pattern, s, repl, n)→ str
 *   regexp.split(pattern, s, n=-1)       → list
 *   regexp.is_match(pattern, s)          → bool
 *
 * Regex instance methods:
 *   re.match(s)   re.search(s)   re.find_all(s)
 *   re.replace(s, repl)          re.split(s, n=-1)
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/table.h"
#include "ms/shape.h"
#include "ms/stdlib/re_engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static MsObjString* intern_s(MsVM* vm, const char* s) {
    return ms_obj_string_copy(vm, s, (int)strlen(s));
}

/* ---- Match class reference (stored in module exports as "Match") ---- */

static MsObjClass* get_match_class(MsVM* vm) {
    /* Look up via the module cache */
    MsObjString* builtin_key = intern_s(vm, "<builtin:regexp>");
    MsValue mod_val;
    if (!ms_table_get(&vm->module_cache, builtin_key, &mod_val)) return NULL;
    MsObjModule* mod = MS_AS_MODULE(mod_val);
    MsValue cv;
    if (!ms_table_get(&mod->exports, intern_s(vm, "Match"), &cv)) return NULL;
    return MS_IS_CLASS(cv) ? MS_AS_CLASS(cv) : NULL;
}

static MsObjClass* get_regex_class(MsVM* vm) {
    MsObjString* builtin_key = intern_s(vm, "<builtin:regexp>");
    MsValue mod_val;
    if (!ms_table_get(&vm->module_cache, builtin_key, &mod_val)) return NULL;
    MsObjModule* mod = MS_AS_MODULE(mod_val);
    MsValue cv;
    if (!ms_table_get(&mod->exports, intern_s(vm, "Regex"), &cv)) return NULL;
    return MS_IS_CLASS(cv) ? MS_AS_CLASS(cv) : NULL;
}

/* ---- Instance field helpers ---- */

static void inst_set(MsVM* vm, MsObjInstance* inst,
                     const char* name, MsValue val) {
    MsObjString* key = intern_s(vm, name);
    int slot = ms_shape_find_slot(inst->shape, key);
    if (slot < 0) {
        inst->shape = ms_shape_transition(vm, inst->shape, key);
        slot = ms_shape_find_slot(inst->shape, key);
    }
    if (slot >= MS_SBO_FIELDS && !inst->overflow_fields) {
        int extra = slot - MS_SBO_FIELDS + 1;
        inst->overflow_fields =
            (MsValue*)calloc((size_t)extra, sizeof(MsValue));
    }
    *ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot) = val;
    if (slot >= inst->field_count) inst->field_count = slot + 1;
}

static MsValue inst_get(MsObjInstance* inst, const char* name) {
    for (int i = 0; i < inst->shape->slots.count; i++) {
        MsSmallEntry* e = &inst->shape->slots.data[i];
        if (e->key && strcmp(e->key->data, name) == 0)
            return *ms_shape_field_ptr(inst->inline_fields,
                                       inst->overflow_fields,
                                       (int)e->value);
    }
    return MS_NIL_VAL();
}

/* ---- Userdata finalizer ---- */

static void free_nfa_ud(void* data) {
    /* Free NFA internals only. object.c calls free(ud->data) after this
     * to free the nfa struct itself — do NOT call re_free() here. */
    re_free_contents((ReNfa*)data);
}

/* ---- Build ReNfa from string pattern ---- */

static ReNfa* compile_pattern(MsVM* vm, MsValue pat_val) {
    if (!MS_IS_STRING(pat_val)) {
        ms_vm_runtime_error(vm, "regexp: pattern must be a string");
        return NULL;
    }
    MsObjString* ps = MS_AS_STRING(pat_val);
    char err[128] = {0};
    ReNfa* nfa = re_compile(ps->data, err);
    if (!nfa) {
        ms_vm_runtime_error(vm, "regexp.compile: %s", err[0] ? err : "invalid pattern");
        return NULL;
    }
    return nfa;
}

/* ---- Get ReNfa* from a Regex instance ---- */

static ReNfa* regex_nfa(MsValue v) {
    if (!MS_IS_INSTANCE(v)) return NULL;
    MsObjInstance* inst = MS_AS_INSTANCE(v);
    if (strcmp(inst->klass->name->data, "Regex") != 0) return NULL;
    MsValue ud_val = inst_get(inst, "_nfa");
    if (!MS_IS_USERDATA(ud_val)) return NULL;
    return (ReNfa*)MS_AS_USERDATA(ud_val)->data;
}

/* ---- Build a Match instance ---- */

static MsValue make_match(MsVM* vm, MsObjClass* klass,
                           const char* s, size_t slen,
                           ReMatch groups[RE_MAX_GROUPS]) {
    (void)slen;
    if (!groups[0].start) return MS_NIL_VAL();

    MsObjInstance* m = ms_obj_instance_new(vm, klass);

    /* matched = groups[0] substring */
    int mlen = (int)(groups[0].end - groups[0].start);
    inst_set(vm, m, "matched",
             MS_OBJ_VAL(ms_obj_string_copy(vm, groups[0].start, mlen)));

    /* start and end (byte offsets relative to s) */
    inst_set(vm, m, "start",
             MS_INT_VAL((ms_i64)(groups[0].start - s)));
    inst_set(vm, m, "end",
             MS_INT_VAL((ms_i64)(groups[0].end - s)));

    /* _groups list: all capture groups (1..n) as strings or nil */
    MsObjList* gl = ms_obj_list_new(vm);
    for (int g = 1; g < RE_MAX_GROUPS; g++) {
        if (!groups[g].start) break;
        int gl_len = (int)(groups[g].end - groups[g].start);
        ms_value_array_push(&gl->items,
            MS_OBJ_VAL(ms_obj_string_copy(vm, groups[g].start, gl_len)));
    }
    inst_set(vm, m, "_groups", MS_OBJ_VAL(gl));

    return MS_OBJ_VAL(m);
}

/* ================================================================== */
/* Match instance methods                                              */
/* ================================================================== */

/* match.groups() → list of captured group strings */
static MsValue match_groups(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_INSTANCE(argv[0])) { ms_vm_runtime_error(vm, "groups: not a Match"); return MS_NIL_VAL(); }
    return inst_get(MS_AS_INSTANCE(argv[0]), "_groups");
}

/* match.group(n) → string or nil; n=0 → matched */
static MsValue match_group(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_INSTANCE(argv[0])) { ms_vm_runtime_error(vm, "group: not a Match"); return MS_NIL_VAL(); }
    ms_i64 n = (argc >= 2) ? MS_AS_INT(argv[1]) : 0;
    if (n == 0) return inst_get(MS_AS_INSTANCE(argv[0]), "matched");
    MsValue gl = inst_get(MS_AS_INSTANCE(argv[0]), "_groups");
    if (!MS_IS_LIST(gl)) return MS_NIL_VAL();
    MsObjList* lst = MS_AS_LIST(gl);
    if (n < 1 || n > lst->items.count) return MS_NIL_VAL();
    return lst->items.data[(int)n - 1];
}

/* ================================================================== */
/* Core search helpers                                                  */
/* ================================================================== */

static MsValue do_match(MsVM* vm, ReNfa* nfa, const char* s, int slen) {
    MsObjClass* klass = get_match_class(vm);
    if (!klass) { ms_vm_runtime_error(vm, "regexp: Match class missing"); return MS_NIL_VAL(); }
    ReMatch groups[RE_MAX_GROUPS];
    memset(groups, 0, sizeof(groups));
    if (!re_match(nfa, s, (size_t)slen, groups)) return MS_NIL_VAL();
    return make_match(vm, klass, s, (size_t)slen, groups);
}

static MsValue do_search(MsVM* vm, ReNfa* nfa, const char* s, int slen) {
    MsObjClass* klass = get_match_class(vm);
    if (!klass) { ms_vm_runtime_error(vm, "regexp: Match class missing"); return MS_NIL_VAL(); }
    ReMatch groups[RE_MAX_GROUPS];
    memset(groups, 0, sizeof(groups));
    if (!re_search(nfa, s, (size_t)slen, groups)) return MS_NIL_VAL();
    return make_match(vm, klass, s, (size_t)slen, groups);
}

static MsValue do_find_all(MsVM* vm, ReNfa* nfa, const char* s, int slen) {
    MsObjClass* klass = get_match_class(vm);
    if (!klass) { ms_vm_runtime_error(vm, "regexp: Match class missing"); return MS_NIL_VAL(); }
    MsObjList* result = ms_obj_list_new(vm);
    size_t offset = 0;
    while (offset <= (size_t)slen) {
        ReMatch groups[RE_MAX_GROUPS];
        memset(groups, 0, sizeof(groups));
        if (!re_search_at(nfa, s, (size_t)slen, offset, groups)) break;
        MsValue m = make_match(vm, klass, s, (size_t)slen, groups);
        ms_value_array_push(&result->items, m);
        size_t match_end = (size_t)(groups[0].end - s);
        if (match_end == offset) offset++;  /* zero-width match: step forward */
        else offset = match_end;
    }
    return MS_OBJ_VAL(result);
}

/* repl: str or fn(Match)->str */
static MsValue do_replace(MsVM* vm, ReNfa* nfa,
                           const char* s, int slen,
                           MsValue repl, ms_i64 max_n) {
    MsObjClass* klass = get_match_class(vm);
    if (!klass) { ms_vm_runtime_error(vm, "regexp: Match class missing"); return MS_NIL_VAL(); }

    /* build result in a heap-grown buffer */
    size_t out_cap = (size_t)slen + 64;
    char* out = (char*)malloc(out_cap);
    if (!out) { ms_vm_runtime_error(vm, "regexp: OOM"); return MS_NIL_VAL(); }
    size_t out_len = 0;

    bool repl_is_str = MS_IS_STRING(repl);
    bool repl_is_fn  = MS_IS_CLOSURE(repl) || MS_IS_NATIVE(repl);
    if (!repl_is_str && !repl_is_fn) {
        free(out);
        ms_vm_runtime_error(vm, "regexp.replace: repl must be str or function");
        return MS_NIL_VAL();
    }

    size_t offset = 0;
    ms_i64 count  = 0;

#define APPEND(data, len) do { \
    size_t _l = (size_t)(len); \
    while (out_len + _l + 1 > out_cap) { \
        out_cap *= 2; \
        char* _t = (char*)realloc(out, out_cap); \
        if (!_t) { free(out); ms_vm_runtime_error(vm, "regexp: OOM"); return MS_NIL_VAL(); } \
        out = _t; \
    } \
    memcpy(out + out_len, (data), _l); out_len += _l; \
} while (0)

    while (offset <= (size_t)slen) {
        if (max_n >= 0 && count >= max_n) {
            APPEND(s + offset, (size_t)slen - offset);
            break;
        }
        ReMatch groups[RE_MAX_GROUPS];
        memset(groups, 0, sizeof(groups));
        if (!re_search_at(nfa, s, (size_t)slen, offset, groups)) {
            APPEND(s + offset, (size_t)slen - offset);
            break;
        }
        /* append text before match */
        size_t match_start = (size_t)(groups[0].start - s);
        APPEND(s + offset, match_start - offset);

        /* compute replacement */
        if (repl_is_str) {
            MsObjString* rs = MS_AS_STRING(repl);
            APPEND(rs->data, (size_t)rs->length);
        } else {
            /* call fn(match) -> str */
            MsValue match = make_match(vm, klass, s, (size_t)slen, groups);
            MsValue result = MS_NIL_VAL();
            if (ms_vm_call_sync(vm, repl, &match, 1, &result) != MS_INTERPRET_OK) {
                free(out); return MS_NIL_VAL();
            }
            if (MS_IS_STRING(result)) {
                MsObjString* rs = MS_AS_STRING(result);
                APPEND(rs->data, (size_t)rs->length);
            }
        }

        size_t match_end = (size_t)(groups[0].end - s);
        count++;
        if (match_end == offset) {
            if (offset < (size_t)slen) APPEND(s + offset, 1);
            offset++;
        } else {
            offset = match_end;
        }
    }
#undef APPEND

    MsValue result = MS_OBJ_VAL(ms_obj_string_copy(vm, out, (int)out_len));
    free(out);
    return result;
}

static MsValue do_split(MsVM* vm, ReNfa* nfa,
                         const char* s, int slen, ms_i64 max_n) {
    MsObjList* result = ms_obj_list_new(vm);
    size_t offset = 0;
    ms_i64 count  = 0;

    while (offset <= (size_t)slen) {
        if (max_n >= 0 && count >= max_n - 1) break;
        ReMatch groups[RE_MAX_GROUPS];
        memset(groups, 0, sizeof(groups));
        if (!re_search_at(nfa, s, (size_t)slen, offset, groups)) break;
        size_t match_start = (size_t)(groups[0].start - s);
        size_t match_end   = (size_t)(groups[0].end   - s);
        int part_len = (int)(match_start - offset);
        ms_value_array_push(&result->items,
            MS_OBJ_VAL(ms_obj_string_copy(vm, s + offset, part_len)));
        count++;
        if (match_end == offset) offset++;
        else offset = match_end;
    }
    /* tail */
    ms_value_array_push(&result->items,
        MS_OBJ_VAL(ms_obj_string_copy(vm, s + offset, slen - (int)offset)));
    return MS_OBJ_VAL(result);
}

/* ================================================================== */
/* Module-level functions                                              */
/* ================================================================== */

/* regexp.compile(pattern) → Regex */
static MsValue regexp_compile(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = compile_pattern(vm, argv[0]);
    if (!nfa) return MS_NIL_VAL();

    MsObjClass* klass = get_regex_class(vm);
    if (!klass) { re_free(nfa); ms_vm_runtime_error(vm, "regexp: Regex class missing"); return MS_NIL_VAL(); }

    MsObjUserdata* ud = ms_obj_userdata_new(vm, 0, free_nfa_ud, NULL, "Regex");
    ud->data = nfa;
    ud->data_size = sizeof(ReNfa);

    MsObjInstance* inst = ms_obj_instance_new(vm, klass);
    inst_set(vm, inst, "_nfa",     MS_OBJ_VAL(ud));
    inst_set(vm, inst, "_pattern", argv[0]);
    return MS_OBJ_VAL(inst);
}

/* regexp.match(pattern, s) → Match|nil */
static MsValue regexp_match(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = compile_pattern(vm, argv[0]);
    if (!nfa) return MS_NIL_VAL();
    if (!MS_IS_STRING(argv[1])) { re_free(nfa); ms_vm_runtime_error(vm, "regexp.match: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    MsValue r = do_match(vm, nfa, ss->data, ss->length);
    re_free(nfa);
    return r;
}

/* regexp.search(pattern, s) → Match|nil */
static MsValue regexp_search(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = compile_pattern(vm, argv[0]);
    if (!nfa) return MS_NIL_VAL();
    if (!MS_IS_STRING(argv[1])) { re_free(nfa); ms_vm_runtime_error(vm, "regexp.search: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    MsValue r = do_search(vm, nfa, ss->data, ss->length);
    re_free(nfa);
    return r;
}

/* regexp.find_all(pattern, s) → list[Match] */
static MsValue regexp_find_all(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = compile_pattern(vm, argv[0]);
    if (!nfa) return MS_NIL_VAL();
    if (!MS_IS_STRING(argv[1])) { re_free(nfa); ms_vm_runtime_error(vm, "regexp.find_all: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    MsValue r = do_find_all(vm, nfa, ss->data, ss->length);
    re_free(nfa);
    return r;
}

/* regexp.replace(pattern, s, repl) */
static MsValue regexp_replace(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = compile_pattern(vm, argv[0]);
    if (!nfa) return MS_NIL_VAL();
    if (!MS_IS_STRING(argv[1])) { re_free(nfa); ms_vm_runtime_error(vm, "regexp.replace: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    MsValue r = do_replace(vm, nfa, ss->data, ss->length, argv[2], -1);
    re_free(nfa);
    return r;
}

/* regexp.replace_n(pattern, s, repl, n) */
static MsValue regexp_replace_n(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = compile_pattern(vm, argv[0]);
    if (!nfa) return MS_NIL_VAL();
    if (!MS_IS_STRING(argv[1])) { re_free(nfa); ms_vm_runtime_error(vm, "regexp.replace_n: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    ms_i64 n = MS_AS_INT(argv[3]);
    MsValue r = do_replace(vm, nfa, ss->data, ss->length, argv[2], n);
    re_free(nfa);
    return r;
}

/* regexp.split(pattern, s, n=-1) */
static MsValue regexp_split(MsVM* vm, int argc, MsValue* argv) {
    ReNfa* nfa = compile_pattern(vm, argv[0]);
    if (!nfa) return MS_NIL_VAL();
    if (!MS_IS_STRING(argv[1])) { re_free(nfa); ms_vm_runtime_error(vm, "regexp.split: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    ms_i64 n = (argc >= 3 && !MS_IS_NIL(argv[2])) ? MS_AS_INT(argv[2]) : -1;
    MsValue r = do_split(vm, nfa, ss->data, ss->length, n);
    re_free(nfa);
    return r;
}

/* regexp.is_match(pattern, s) → bool */
static MsValue regexp_is_match(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = compile_pattern(vm, argv[0]);
    if (!nfa) return MS_NIL_VAL();
    if (!MS_IS_STRING(argv[1])) { re_free(nfa); ms_vm_runtime_error(vm, "regexp.is_match: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    ReMatch groups[RE_MAX_GROUPS];
    memset(groups, 0, sizeof(groups));
    bool found = re_search(nfa, ss->data, (size_t)ss->length, groups);
    re_free(nfa);
    return MS_BOOL_VAL(found);
}

/* ================================================================== */
/* Regex instance methods                                              */
/* ================================================================== */

static MsValue regex_match(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = regex_nfa(argv[0]);
    if (!nfa) { ms_vm_runtime_error(vm, "re.match: not a Regex"); return MS_NIL_VAL(); }
    if (!MS_IS_STRING(argv[1])) { ms_vm_runtime_error(vm, "re.match: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    return do_match(vm, nfa, ss->data, ss->length);
}

static MsValue regex_search(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = regex_nfa(argv[0]);
    if (!nfa) { ms_vm_runtime_error(vm, "re.search: not a Regex"); return MS_NIL_VAL(); }
    if (!MS_IS_STRING(argv[1])) { ms_vm_runtime_error(vm, "re.search: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    return do_search(vm, nfa, ss->data, ss->length);
}

static MsValue regex_find_all(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = regex_nfa(argv[0]);
    if (!nfa) { ms_vm_runtime_error(vm, "re.find_all: not a Regex"); return MS_NIL_VAL(); }
    if (!MS_IS_STRING(argv[1])) { ms_vm_runtime_error(vm, "re.find_all: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    return do_find_all(vm, nfa, ss->data, ss->length);
}

static MsValue regex_replace(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    ReNfa* nfa = regex_nfa(argv[0]);
    if (!nfa) { ms_vm_runtime_error(vm, "re.replace: not a Regex"); return MS_NIL_VAL(); }
    if (!MS_IS_STRING(argv[1])) { ms_vm_runtime_error(vm, "re.replace: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    return do_replace(vm, nfa, ss->data, ss->length, argv[2], -1);
}

static MsValue regex_split(MsVM* vm, int argc, MsValue* argv) {
    ReNfa* nfa = regex_nfa(argv[0]);
    if (!nfa) { ms_vm_runtime_error(vm, "re.split: not a Regex"); return MS_NIL_VAL(); }
    if (!MS_IS_STRING(argv[1])) { ms_vm_runtime_error(vm, "re.split: s must be string"); return MS_NIL_VAL(); }
    MsObjString* ss = MS_AS_STRING(argv[1]);
    ms_i64 n = (argc >= 3 && !MS_IS_NIL(argv[2])) ? MS_AS_INT(argv[2]) : -1;
    return do_split(vm, nfa, ss->data, ss->length, n);
}

/* ================================================================== */
/* Module init                                                          */
/* ================================================================== */

void ms_module_regexp_init(MsVM* vm, MsObjModule* mod) {
    /* 1. Create Match class */
    MsObjClass* match_klass = ms_obj_class_new(vm, intern_s(vm, "Match"));
    static const struct { const char* name; MsNativeFn fn; int arity; } match_methods[] = {
        {"groups", match_groups, 1},
        {"group",  match_group,  -1},
        {NULL, NULL, 0}
    };
    for (int i = 0; match_methods[i].name; i++) {
        MsObjNative* nat = ms_obj_native_new(vm, match_methods[i].fn,
                                              match_methods[i].name,
                                              match_methods[i].arity);
        ms_table_set(&match_klass->methods,
                     intern_s(vm, match_methods[i].name),
                     MS_OBJ_VAL(nat));
    }
    ms_module_export_value(vm, mod, "Match", MS_OBJ_VAL(match_klass));

    /* 2. Create Regex class */
    MsObjClass* regex_klass = ms_obj_class_new(vm, intern_s(vm, "Regex"));
    static const struct { const char* name; MsNativeFn fn; int arity; } re_methods[] = {
        {"match",    regex_match,    2},
        {"search",   regex_search,   2},
        {"find_all", regex_find_all, 2},
        {"replace",  regex_replace,  3},
        {"split",    regex_split,    -1},
        {NULL, NULL, 0}
    };
    for (int i = 0; re_methods[i].name; i++) {
        MsObjNative* nat = ms_obj_native_new(vm, re_methods[i].fn,
                                              re_methods[i].name,
                                              re_methods[i].arity);
        ms_table_set(&regex_klass->methods,
                     intern_s(vm, re_methods[i].name),
                     MS_OBJ_VAL(nat));
    }
    ms_module_export_value(vm, mod, "Regex", MS_OBJ_VAL(regex_klass));

    /* 3. Module-level functions */
    static const MsNativeDef defs[] = {
        {"compile",   regexp_compile,   1},
        {"match",     regexp_match,     2},
        {"search",    regexp_search,    2},
        {"find_all",  regexp_find_all,  2},
        {"replace",   regexp_replace,   3},
        {"replace_n", regexp_replace_n, 4},
        {"split",     regexp_split,    -1},
        {"is_match",  regexp_is_match,  2},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);
}
