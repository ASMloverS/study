/* src/stdlib/template.c - STDLIB-46: template module
 *
 * Data-driven text templates: variable substitution, conditionals, loops.
 * Hand-written parser, no regex dependency.
 *
 * Public API:
 *   template.parse(src)            → Template userdata
 *   template.render(src, ctx)      → str  (one-shot parse+render)
 *   template.render_tmpl(t, ctx)   → str  (render compiled template)
 *
 * Template syntax:
 *   {{name}}                variable substitution
 *   {{= expr }}             expression (treated as variable lookup)
 *   {{if cond}} ... {{end}}
 *   {{if cond}} ... {{else}} ... {{end}}
 *   {{for item in list}} ... {{end}}
 *   {{for k, v in map}} ... {{end}}
 *   {{! comment }}          no output
 *   \{{                     literal {{
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/vtable.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Portable string duplicate (avoids MSVC deprecation warning for strdup) */
static char* tmpl_strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* r = (char*)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

/* ================================================================
 * Node types
 * ================================================================ */

typedef enum {
    TN_TEXT,      /* literal text */
    TN_VAR,       /* {{name}} */
    TN_EXPR,      /* {{= expr }} — treated as variable lookup */
    TN_COMMENT,   /* {{! ... }} — no output */
    TN_IF,        /* {{if cond}} — jump = else_idx or end_idx */
    TN_ELSE,      /* {{else}}   — jump = end_idx */
    TN_END,       /* {{end}} closing if */
    TN_FOR_LIST,  /* {{for item in list}} — text=src_name, text2=item_name */
    TN_FOR_MAP,   /* {{for k, v in map}} — text=k_name, text2="v_name\xffsrc_name" */
    TN_ENDFOR,    /* {{end}} closing for — back=matching FOR index */
} TNodeType;

typedef struct {
    TNodeType type;
    char*     text;   /* TN_TEXT: content; TN_VAR/EXPR/IF: key/cond;
                         TN_FOR_LIST: source list var; TN_FOR_MAP: k var name */
    char*     text2;  /* TN_FOR_LIST: item var name;
                         TN_FOR_MAP: packed "vname\xffsrc_name" */
    int       jump;   /* TN_IF: else or end idx; TN_ELSE: end idx;
                         TN_FOR_LIST/MAP: endfor idx */
    int       back;   /* TN_ENDFOR: index of matching TN_FOR* */
} TNode;

/* ================================================================
 * Template userdata
 * ================================================================ */

#define TEMPLATE_TAG "template.Template"

typedef struct {
    TNode* nodes;
    int    count;
    int    cap;
} TemplateState;

static void template_finalize(void* d) {
    TemplateState* s = (TemplateState*)d;
    for (int i = 0; i < s->count; i++) {
        free(s->nodes[i].text);
        free(s->nodes[i].text2);
    }
    free(s->nodes);
}

static void template_mark(MsVM* vm, void* d) {
    (void)vm; (void)d;
}

/* ================================================================
 * Dynamic output buffer
 * ================================================================ */

typedef struct { char* data; int len; int cap; } OutBuf;

static void outbuf_init(OutBuf* b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void outbuf_free(OutBuf* b) { free(b->data); }

static bool outbuf_append(OutBuf* b, const char* s, int n) {
    if (n <= 0) return true;
    if (b->len + n >= b->cap) {
        int nc = b->cap < 64 ? 64 : b->cap * 2;
        while (nc < b->len + n + 1) nc *= 2;
        char* nd = (char*)realloc(b->data, (size_t)nc);
        if (!nd) return false;
        b->data = nd; b->cap = nc;
    }
    memcpy(b->data + b->len, s, (size_t)n);
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

static bool outbuf_appends(OutBuf* b, const char* s) {
    return outbuf_append(b, s, (int)strlen(s));
}

/* ================================================================
 * Value → string (uses caller-supplied numbuf for numeric values)
 * ================================================================ */

static const char* val_str(MsValue v, char* nbuf, int nbsz) {
    if (MS_IS_STRING(v))  return MS_AS_CSTRING(v);
    if (MS_IS_NIL(v))     return "";
    if (MS_IS_BOOL(v))    return v.as.boolean ? "true" : "false";
    if (MS_IS_INT(v))   {
        snprintf(nbuf, (size_t)nbsz, "%lld", (long long)v.as.integer);
        return nbuf;
    }
    if (MS_IS_NUMBER(v)) {
        double d = v.as.number;
        if (d == (long long)d)
            snprintf(nbuf, (size_t)nbsz, "%lld", (long long)d);
        else
            snprintf(nbuf, (size_t)nbsz, "%g", d);
        return nbuf;
    }
    return "";
}

/* Truthiness for template conditions */
static bool is_truthy(MsValue v) {
    if (MS_IS_NIL(v))    return false;
    if (MS_IS_BOOL(v))   return v.as.boolean;
    if (MS_IS_INT(v))    return v.as.integer != 0;
    if (MS_IS_NUMBER(v)) return v.as.number != 0.0;
    if (MS_IS_STRING(v)) return MS_AS_STRING(v)->length > 0;
    return true;
}

/* ================================================================
 * Parser helpers
 * ================================================================ */

/* Heap-allocate a trimmed copy of s[0..len). Caller must free. */
static char* trim_dup(const char* s, int len) {
    while (len > 0 && (s[0] == ' ' || s[0] == '\t')) { s++; len--; }
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    char* r = (char*)malloc((size_t)len + 1);
    if (!r) return NULL;
    memcpy(r, s, (size_t)len);
    r[len] = '\0';
    return r;
}

static void node_push(TemplateState* s, TNode n) {
    if (s->count == s->cap) {
        int nc = s->cap < 16 ? 16 : s->cap * 2;
        TNode* nb = (TNode*)realloc(s->nodes, (size_t)nc * sizeof(TNode));
        if (!nb) abort();
        s->nodes = nb; s->cap = nc;
    }
    s->nodes[s->count++] = n;
}

/* ================================================================
 * Parser: src[0..slen) → TemplateState node array
 * ================================================================ */

#define PARSE_STACK_MAX 64

static bool parse_template(TemplateState* s, const char* src, int slen,
                            char* errbuf, int ebsz) {
    int  stk[PARSE_STACK_MAX];
    int  stk_top = 0;
    const char* p   = src;
    const char* end = src + slen;

    while (p < end) {
        /* Escape: \{{ → literal {{ */
        if (p[0] == '\\' && p+2 < end && p[1] == '{' && p[2] == '{') {
            TNode n; memset(&n, 0, sizeof(n));
            n.type = TN_TEXT; n.text = tmpl_strdup("{{");
            node_push(s, n);
            p += 3; continue;
        }

        if (p[0] == '{' && p+1 < end && p[1] == '{') {
            /* Find closing }} */
            const char* inner = p + 2;
            const char* close = NULL;
            for (const char* q = inner; q + 1 < end; q++)
                if (q[0] == '}' && q[1] == '}') { close = q; break; }
            if (!close) {
                snprintf(errbuf, (size_t)ebsz, "template: unclosed {{ at offset %d",
                         (int)(p - src));
                return false;
            }
            char* tag = trim_dup(inner, (int)(close - inner));
            if (!tag) { snprintf(errbuf,(size_t)ebsz,"OOM"); return false; }
            p = close + 2;

            TNode n; memset(&n, 0, sizeof(n));

            /* Comment */
            if (tag[0] == '!') {
                n.type = TN_COMMENT; n.text = tag;
                node_push(s, n);

            /* Expression */
            } else if (tag[0] == '=') {
                n.type = TN_EXPR;
                n.text = trim_dup(tag + 1, (int)strlen(tag + 1));
                free(tag);
                if (!n.text) n.text = tmpl_strdup("");
                node_push(s, n);

            /* if */
            } else if (strncmp(tag, "if ", 3) == 0) {
                n.type = TN_IF;
                n.text = trim_dup(tag + 3, (int)strlen(tag + 3));
                free(tag);
                if (!n.text) n.text = tmpl_strdup("");
                n.jump = -1;
                int idx = s->count;
                node_push(s, n);
                if (stk_top >= PARSE_STACK_MAX) {
                    snprintf(errbuf,(size_t)ebsz,"template: nesting too deep");
                    return false;
                }
                stk[stk_top++] = idx;

            /* else */
            } else if (strcmp(tag, "else") == 0) {
                free(tag);
                if (stk_top == 0 || s->nodes[stk[stk_top-1]].type != TN_IF) {
                    snprintf(errbuf,(size_t)ebsz,"template: {{else}} without {{if}}");
                    return false;
                }
                int if_idx = stk[stk_top - 1];
                s->nodes[if_idx].jump = s->count; /* if.jump → else */
                n.type = TN_ELSE; n.jump = -1;
                int idx = s->count;
                node_push(s, n);
                stk[stk_top - 1] = idx;

            /* end */
            } else if (strcmp(tag, "end") == 0) {
                free(tag);
                if (stk_top == 0) {
                    snprintf(errbuf,(size_t)ebsz,"template: {{end}} without open block");
                    return false;
                }
                int open_idx = stk[--stk_top];
                TNodeType ot = s->nodes[open_idx].type;
                if (ot == TN_IF) {
                    s->nodes[open_idx].jump = s->count;
                    n.type = TN_END; n.jump = open_idx;
                    node_push(s, n);
                } else if (ot == TN_ELSE) {
                    s->nodes[open_idx].jump = s->count;
                    n.type = TN_END; n.jump = open_idx;
                    node_push(s, n);
                } else if (ot == TN_FOR_LIST || ot == TN_FOR_MAP) {
                    s->nodes[open_idx].jump = s->count;
                    n.type = TN_ENDFOR; n.back = open_idx;
                    node_push(s, n);
                } else {
                    snprintf(errbuf,(size_t)ebsz,"template: unexpected {{end}}");
                    return false;
                }

            /* for */
            } else if (strncmp(tag, "for ", 4) == 0) {
                const char* body   = tag + 4;
                const char* in_ptr = strstr(body, " in ");
                if (!in_ptr) {
                    snprintf(errbuf,(size_t)ebsz,"template: malformed {{for}}: missing ' in '");
                    free(tag); return false;
                }
                char* vars_part = trim_dup(body, (int)(in_ptr - body));
                char* src_part  = trim_dup(in_ptr + 4, (int)strlen(in_ptr + 4));
                free(tag);
                if (!vars_part || !src_part) {
                    free(vars_part); free(src_part);
                    snprintf(errbuf,(size_t)ebsz,"OOM"); return false;
                }

                char* comma = strchr(vars_part, ',');
                if (comma) {
                    /* map loop: "k, v in src" */
                    *comma = '\0';
                    char* kvar = trim_dup(vars_part, (int)strlen(vars_part));
                    char* vvar = trim_dup(comma + 1, (int)strlen(comma + 1));
                    free(vars_part);
                    if (!kvar || !vvar) {
                        free(kvar); free(vvar); free(src_part);
                        snprintf(errbuf,(size_t)ebsz,"OOM"); return false;
                    }
                    /* Pack "vvar\xffsrc" into text2 */
                    char* packed = (char*)malloc(strlen(vvar) + 1 + strlen(src_part) + 1);
                    if (!packed) {
                        free(kvar); free(vvar); free(src_part);
                        snprintf(errbuf,(size_t)ebsz,"OOM"); return false;
                    }
                    snprintf(packed, strlen(vvar) + 1 + strlen(src_part) + 1,
                             "%s\xff%s", vvar, src_part);
                    free(vvar); free(src_part);
                    n.type = TN_FOR_MAP; n.text = kvar; n.text2 = packed;
                    n.jump = -1;
                } else {
                    /* list loop: "item in src" */
                    n.type = TN_FOR_LIST; n.text = src_part; n.text2 = vars_part;
                    n.jump = -1;
                }
                int idx = s->count;
                node_push(s, n);
                if (stk_top >= PARSE_STACK_MAX) {
                    snprintf(errbuf,(size_t)ebsz,"template: nesting too deep");
                    return false;
                }
                stk[stk_top++] = idx;

            /* plain variable */
            } else {
                n.type = TN_VAR; n.text = tag;
                node_push(s, n);
            }
        } else {
            /* Collect literal text until next {{ or \{{ */
            const char* start = p;
            while (p < end) {
                if (p[0] == '\\' && p+2 < end && p[1] == '{' && p[2] == '{') break;
                if (p[0] == '{' && p+1 < end && p[1] == '{') break;
                p++;
            }
            if (p > start) {
                TNode n; memset(&n, 0, sizeof(n));
                n.type = TN_TEXT;
                n.text = (char*)malloc((size_t)(p - start) + 1);
                if (!n.text) { snprintf(errbuf,(size_t)ebsz,"OOM"); return false; }
                memcpy(n.text, start, (size_t)(p - start));
                n.text[p - start] = '\0';
                node_push(s, n);
            }
        }
    }
    if (stk_top != 0) {
        snprintf(errbuf,(size_t)ebsz,"template: unclosed block (depth=%d)", stk_top);
        return false;
    }
    return true;
}
#undef PARSE_STACK_MAX

/* ================================================================
 * Render scope: loop-variable injection
 * ================================================================ */

typedef struct RenderScope {
    const char*         key_name;
    MsValue             key_val;
    const char*         val_name; /* NULL for list loops */
    MsValue             val_val;
    struct RenderScope* parent;
} RenderScope;

static MsValue scope_lookup(RenderScope* sc, MsObjMap* ctx, MsVM* vm,
                             const char* name) {
    for (RenderScope* s = sc; s; s = s->parent) {
        if (s->key_name && strcmp(s->key_name, name) == 0) return s->key_val;
        if (s->val_name && strcmp(s->val_name, name) == 0) return s->val_val;
    }
    MsObjString* ks = ms_obj_string_copy(vm, name, (int)strlen(name));
    MsValue out;
    if (ms_vtable_get(&ctx->table, MS_OBJ_VAL(ks), &out)) return out;
    return MS_NIL_VAL();
}

/* ================================================================
 * Render engine
 * ================================================================ */

static bool render_nodes(TNode* nodes, int count, int* ip,
                         RenderScope* scope, MsObjMap* ctx, MsVM* vm,
                         OutBuf* out, int stop_at,
                         char* errbuf, int ebsz);

static bool render_nodes(TNode* nodes, int count, int* ip,
                         RenderScope* scope, MsObjMap* ctx, MsVM* vm,
                         OutBuf* out, int stop_at,
                         char* errbuf, int ebsz) {
    char nbuf[64];
    while (*ip < count && *ip != stop_at) {
        TNode* n = &nodes[*ip];
        switch (n->type) {

            case TN_TEXT:
                outbuf_appends(out, n->text);
                (*ip)++; break;

            case TN_COMMENT:
                (*ip)++; break;

            case TN_VAR:
            case TN_EXPR: {
                MsValue v = scope_lookup(scope, ctx, vm, n->text);
                outbuf_appends(out, val_str(v, nbuf, (int)sizeof(nbuf)));
                (*ip)++; break;
            }

            case TN_IF: {
                int cond_idx = *ip;
                int jump     = nodes[cond_idx].jump;  /* else or end */
                MsValue cond = scope_lookup(scope, ctx, vm, nodes[cond_idx].text);
                (*ip)++;
                if (is_truthy(cond)) {
                    /* render true branch up to jump */
                    if (!render_nodes(nodes, count, ip, scope, ctx, vm,
                                      out, jump, errbuf, ebsz))
                        return false;
                    /* if we landed on ELSE, skip else body */
                    if (*ip < count && nodes[*ip].type == TN_ELSE) {
                        int end_idx = nodes[*ip].jump;
                        *ip = end_idx + 1;
                    } else {
                        (*ip)++; /* skip TN_END */
                    }
                } else {
                    *ip = jump;
                    if (*ip < count && nodes[*ip].type == TN_ELSE) {
                        int else_body = *ip + 1;
                        int end_idx   = nodes[*ip].jump;
                        *ip = else_body;
                        if (!render_nodes(nodes, count, ip, scope, ctx, vm,
                                          out, end_idx, errbuf, ebsz))
                            return false;
                        *ip = end_idx + 1;
                    } else {
                        /* no else: jump points to TN_END */
                        *ip = jump + 1;
                    }
                }
                break;
            }

            case TN_ELSE:
            case TN_END:
            case TN_ENDFOR:
                return true;  /* stop_at caller handles advancing ip */

            case TN_FOR_LIST: {
                const char* src_name  = n->text;
                const char* item_name = n->text2;
                int body_start = *ip + 1;
                int end_idx    = n->jump;
                (*ip)++;

                MsValue list_val = scope_lookup(scope, ctx, vm, src_name);
                if (!MS_IS_LIST(list_val)) { *ip = end_idx + 1; break; }
                MsObjList* lst = MS_AS_LIST(list_val);
                for (int j = 0; j < lst->items.count; j++) {
                    RenderScope sc;
                    sc.key_name = item_name;
                    sc.key_val  = lst->items.data[j];
                    sc.val_name = NULL;
                    sc.parent   = scope;
                    int pos = body_start;
                    if (!render_nodes(nodes, count, &pos, &sc, ctx, vm,
                                      out, end_idx, errbuf, ebsz))
                        return false;
                }
                *ip = end_idx + 1;
                break;
            }

            case TN_FOR_MAP: {
                const char* k_name  = n->text;
                const char* packed  = n->text2;  /* "vname\xffsrc_name" */
                const char* sep     = packed ? strchr(packed, '\xff') : NULL;
                int vname_len       = sep ? (int)(sep - packed) : 0;
                char vname_buf[128]; vname_buf[0] = '\0';
                if (sep && vname_len < (int)sizeof(vname_buf)) {
                    memcpy(vname_buf, packed, (size_t)vname_len);
                    vname_buf[vname_len] = '\0';
                }
                const char* src_name = sep ? sep + 1 : "";
                int body_start = *ip + 1;
                int end_idx    = n->jump;
                (*ip)++;

                MsValue map_val = scope_lookup(scope, ctx, vm, src_name);
                if (!MS_IS_MAP(map_val)) { *ip = end_idx + 1; break; }
                MsObjMap* m = MS_AS_MAP(map_val);
                for (int j = 0; j < m->table.capacity; j++) {
                    MsVEntry* e = &m->table.entries[j];
                    if (!e->used || e->tombstone) continue;
                    RenderScope sc;
                    sc.key_name = k_name;
                    sc.key_val  = e->key;
                    sc.val_name = vname_buf[0] ? vname_buf : NULL;
                    sc.val_val  = e->value;
                    sc.parent   = scope;
                    int pos = body_start;
                    if (!render_nodes(nodes, count, &pos, &sc, ctx, vm,
                                      out, end_idx, errbuf, ebsz))
                        return false;
                }
                *ip = end_idx + 1;
                break;
            }
        }
    }
    return true;
}

/* ================================================================
 * Core: render a TemplateState against a context map
 * ================================================================ */

static MsValue do_render(MsVM* vm, TemplateState* s, MsObjMap* ctx) {
    OutBuf buf; outbuf_init(&buf);
    char errbuf[256]; errbuf[0] = '\0';
    int ip = 0;
    bool ok = render_nodes(s->nodes, s->count, &ip, NULL, ctx, vm,
                           &buf, -1, errbuf, (int)sizeof(errbuf));
    if (!ok) {
        outbuf_free(&buf);
        ms_vm_runtime_error(vm, "%s", errbuf);
        return MS_NIL_VAL();
    }
    const char* result = buf.data ? buf.data : "";
    MsObjString* rs = ms_obj_string_copy(vm, result, buf.len);
    outbuf_free(&buf);
    return MS_OBJ_VAL(rs);
}

/* ================================================================
 * template.parse(src) → Template userdata
 * ================================================================ */

static MsValue tmpl_parse(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "template.parse: src must be a string");
        return MS_NIL_VAL();
    }
    MsObjString* ss = MS_AS_STRING(argv[0]);
    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(TemplateState),
                                            template_finalize, template_mark,
                                            TEMPLATE_TAG);
    TemplateState* s = (TemplateState*)ud->data;
    memset(s, 0, sizeof(TemplateState));
    char errbuf[256];
    if (!parse_template(s, ss->data, ss->length, errbuf, (int)sizeof(errbuf))) {
        ms_vm_runtime_error(vm, "%s", errbuf);
        return MS_NIL_VAL();
    }
    return MS_OBJ_VAL(ud);
}

/* ================================================================
 * template.render_tmpl(t, ctx) → str
 * (Used by the test harness and as the t.render(ctx) equivalent)
 * ================================================================ */

static MsValue tmpl_render_tmpl(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_USERDATA(argv[0]) ||
        strcmp(MS_AS_USERDATA(argv[0])->type_tag, TEMPLATE_TAG) != 0) {
        ms_vm_runtime_error(vm, "template.render_tmpl: first arg must be a Template");
        return MS_NIL_VAL();
    }
    if (!MS_IS_MAP(argv[1])) {
        ms_vm_runtime_error(vm, "template.render_tmpl: ctx must be a map");
        return MS_NIL_VAL();
    }
    TemplateState* s = (TemplateState*)MS_AS_USERDATA(argv[0])->data;
    return do_render(vm, s, MS_AS_MAP(argv[1]));
}

/* ================================================================
 * template.render(src, ctx) → str   (one-shot convenience)
 * ================================================================ */

static MsValue tmpl_render(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "template.render: src must be a string");
        return MS_NIL_VAL();
    }
    if (!MS_IS_MAP(argv[1])) {
        ms_vm_runtime_error(vm, "template.render: ctx must be a map");
        return MS_NIL_VAL();
    }
    MsObjString* ss = MS_AS_STRING(argv[0]);
    TemplateState ts; memset(&ts, 0, sizeof(ts));
    char errbuf[256];
    if (!parse_template(&ts, ss->data, ss->length, errbuf, (int)sizeof(errbuf))) {
        for (int i = 0; i < ts.count; i++) { free(ts.nodes[i].text); free(ts.nodes[i].text2); }
        free(ts.nodes);
        ms_vm_runtime_error(vm, "%s", errbuf);
        return MS_NIL_VAL();
    }
    MsValue result = do_render(vm, &ts, MS_AS_MAP(argv[1]));
    for (int i = 0; i < ts.count; i++) { free(ts.nodes[i].text); free(ts.nodes[i].text2); }
    free(ts.nodes);
    return result;
}

/* ================================================================
 * Module init
 * ================================================================ */

static const MsNativeDef template_defs[] = {
    {"parse",        tmpl_parse,        1},
    {"render",       tmpl_render,       2},
    {"render_tmpl",  tmpl_render_tmpl,  2},
    {NULL, NULL, 0}
};

void ms_module_template_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, template_defs);
}
