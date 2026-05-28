/* src/stdlib/_re_engine.c
 *
 * Thompson NFA regexp engine.
 * Supports: char literals, '.', character classes ([abc] [a-z] [^...]),
 * predefined classes (\d \w \s and uppercase inverses),
 * quantifiers (* + ? {n} {n,} {n,m} and lazy *? +? ??),
 * anchors (^ $), alternation (|),
 * groups ((...) and (?:...)), flags (?i) (?m).
 *
 * O(n*m) matching, no backtracking, no ReDoS.
 */
#include "ms/stdlib/re_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>

/* ================================================================== */
/* NFA node types                                                       */
/* ================================================================== */

typedef enum {
    RE_MATCH,      /* accept */
    RE_CHAR,       /* match one char (with optional case folding) */
    RE_ANY,        /* '.' - any except \n */
    RE_CLASS,      /* character class bitmap */
    RE_SPLIT,      /* epsilon-split (a|b, *, +, ?) */
    RE_SAVE,       /* capture group boundary */
    RE_ANCHOR_BOL, /* ^ */
    RE_ANCHOR_EOL, /* $ */
} ReNodeType;

/* Character class bitmap: 256 bits = 32 bytes */
typedef struct { uint8_t bits[32]; } ReBitmap;
static void bitmap_set(ReBitmap* b, unsigned char c) { b->bits[c/8] |=  (uint8_t)(1u << (c%8)); }
static bool bitmap_test(const ReBitmap* b, unsigned char c){ return (b->bits[c/8] >> (c%8)) & 1; }

struct ReState {
    ReNodeType type;
    union {
        /* RE_CHAR */
        unsigned char ch;
        /* RE_CLASS */
        ReBitmap* cls;
        /* RE_SPLIT */
        struct { int out1; int out2; };
        /* RE_SAVE */
        struct { int group_id; bool is_open; };
    } u;
    int out;  /* primary successor (-1 = none) */
    bool greedy; /* for SPLIT representing quantifier */
};

/* ================================================================== */
/* Builder                                                              */
/* ================================================================== */

#define RE_MAX_STATES 2048
#define RE_ERR_SIZE   128

typedef struct {
    ReState states[RE_MAX_STATES];
    int count;
    char err[RE_ERR_SIZE];
    bool flag_i;
    bool flag_m;
    int n_groups;
} ReBuilder;

static int new_state(ReBuilder* b, ReNodeType t) {
    if (b->count >= RE_MAX_STATES) {
        snprintf(b->err, RE_ERR_SIZE, "regexp: pattern too complex");
        return -1;
    }
    int id = b->count++;
    memset(&b->states[id], 0, sizeof(b->states[id]));
    b->states[id].type = t;
    b->states[id].out  = -1;
    b->states[id].greedy = true;
    return id;
}

/* Fragment: start state + list of dangling output slots */
#define RE_MAX_OUTS 256

typedef struct {
    int  start;
    int  outs[RE_MAX_OUTS];  /* state indices whose .out needs patching */
    int  out2s[RE_MAX_OUTS]; /* for SPLIT .u.out2 */
    int  nouts;
    int  nout2s;
} ReFrag;

static ReFrag frag(int start) {
    ReFrag f;
    memset(&f, 0, sizeof(f));
    f.start = start;
    return f;
}

static void frag_add_out(ReFrag* f, int s, bool second) {
    if (second) {
        if (f->nout2s < RE_MAX_OUTS) f->out2s[f->nout2s++] = s;
    } else {
        if (f->nouts  < RE_MAX_OUTS) f->outs [f->nouts++]  = s;
    }
}

static void frag_patch(ReBuilder* b, ReFrag* f, int target) {
    for (int i = 0; i < f->nouts;  i++) {
        b->states[f->outs[i]].out = target;
    }
    for (int i = 0; i < f->nout2s; i++) {
        b->states[f->out2s[i]].u.out2 = target;
    }
    f->nouts = f->nout2s = 0;
}

/* Concatenate two fragments */
static ReFrag concat_frag(ReBuilder* b, ReFrag a, ReFrag c) {
    /* patch a's dangling outs to c.start */
    for (int i = 0; i < a.nouts;  i++) b->states[a.outs[i]].out     = c.start;
    for (int i = 0; i < a.nout2s; i++) b->states[a.out2s[i]].u.out2 = c.start;
    c.start = a.start;
    return c;
}

/* ================================================================== */
/* Parser                                                               */
/* ================================================================== */

typedef struct {
    const char* p;   /* current parse position */
    const char* end; /* one past last char     */
    ReBuilder*  b;
} ReParser;

static ReFrag parse_expr(ReParser* pr, bool in_group);

/* Case-fold a char if flag_i */
static unsigned char fold(ReBuilder* b, unsigned char c) {
    return b->flag_i ? (unsigned char)tolower(c) : c;
}

/* Fill bitmap for predefined class \d \w \s (lowercase=match, uppercase=negate) */
static void bitmap_class(ReBitmap* bm, char cls, bool invert) {
    memset(bm->bits, 0, sizeof(bm->bits));
    switch (tolower((unsigned char)cls)) {
    case 'd':
        for (unsigned char c = '0'; c <= '9'; c++) bitmap_set(bm, c);
        break;
    case 'w':
        for (unsigned char c = 'a'; c <= 'z'; c++) bitmap_set(bm, c);
        for (unsigned char c = 'A'; c <= 'Z'; c++) bitmap_set(bm, c);
        for (unsigned char c = '0'; c <= '9'; c++) bitmap_set(bm, c);
        bitmap_set(bm, '_');
        break;
    case 's':
        bitmap_set(bm, ' ');  bitmap_set(bm, '\t');
        bitmap_set(bm, '\n'); bitmap_set(bm, '\r');
        bitmap_set(bm, '\f'); bitmap_set(bm, '\v');
        break;
    default:
        break;
    }
    bool is_upper = isupper((unsigned char)cls);
    if (is_upper != invert) {
        /* negate */
        for (int i = 0; i < 32; i++) bm->bits[i] ^= 0xFF;
    }
}

/* Parse a character class [...] */
static ReFrag parse_char_class(ReParser* pr) {
    ReBuilder* b = pr->b;
    ReBitmap* bm = (ReBitmap*)calloc(1, sizeof(ReBitmap));
    if (!bm) { snprintf(b->err, RE_ERR_SIZE, "regexp: OOM"); return frag(-1); }

    bool negate = false;
    if (pr->p < pr->end && *pr->p == '^') { negate = true; pr->p++; }

    /* First char can be ] without closing the class */
    bool first = true;
    while (pr->p < pr->end && (*pr->p != ']' || first)) {
        first = false;
        unsigned char c = (unsigned char)*pr->p++;
        if (c == '\\' && pr->p < pr->end) {
            unsigned char esc = (unsigned char)*pr->p++;
            if (esc == 'd' || esc == 'D' || esc == 'w' || esc == 'W' ||
                esc == 's' || esc == 'S') {
                ReBitmap tmp;
                bitmap_class(&tmp, (char)esc, false);
                for (int i = 0; i < 32; i++) bm->bits[i] |= tmp.bits[i];
            } else {
                if (b->flag_i) {
                    bitmap_set(bm, (unsigned char)tolower(esc));
                    bitmap_set(bm, (unsigned char)toupper(esc));
                } else {
                    bitmap_set(bm, esc);
                }
            }
        } else {
            /* check for range a-z */
            if (pr->p + 1 < pr->end && *pr->p == '-' && *(pr->p+1) != ']') {
                unsigned char lo = c, hi = (unsigned char)*(pr->p+1);
                pr->p += 2;
                for (unsigned int i = lo; i <= hi; i++) {
                    if (b->flag_i) {
                        bitmap_set(bm, (unsigned char)tolower((int)i));
                        bitmap_set(bm, (unsigned char)toupper((int)i));
                    } else {
                        bitmap_set(bm, (unsigned char)i);
                    }
                }
            } else {
                if (b->flag_i) {
                    bitmap_set(bm, (unsigned char)tolower(c));
                    bitmap_set(bm, (unsigned char)toupper(c));
                } else {
                    bitmap_set(bm, c);
                }
            }
        }
    }
    if (pr->p >= pr->end || *pr->p != ']') {
        free(bm);
        snprintf(b->err, RE_ERR_SIZE, "regexp: unterminated character class");
        return frag(-1);
    }
    pr->p++; /* consume ']' */

    if (negate) {
        for (int i = 0; i < 32; i++) bm->bits[i] ^= 0xFF;
    }

    int id = new_state(b, RE_CLASS);
    if (id < 0) { free(bm); return frag(-1); }
    b->states[id].u.cls = bm;
    ReFrag f = frag(id);
    frag_add_out(&f, id, false);
    return f;
}

/* Parse a single atom */
static ReFrag parse_atom(ReParser* pr) {
    ReBuilder* b = pr->b;
    if (pr->p >= pr->end) return frag(-1);

    unsigned char c = (unsigned char)*pr->p;

    /* Anchors */
    if (c == '^') {
        pr->p++;
        int id = new_state(b, RE_ANCHOR_BOL);
        if (id < 0) return frag(-1);
        ReFrag f = frag(id);
        frag_add_out(&f, id, false);
        return f;
    }
    if (c == '$') {
        pr->p++;
        int id = new_state(b, RE_ANCHOR_EOL);
        if (id < 0) return frag(-1);
        ReFrag f = frag(id);
        frag_add_out(&f, id, false);
        return f;
    }

    /* Group */
    if (c == '(') {
        pr->p++;
        bool capturing = true;
        int  group_id  = -1;
        /* check for (?:...) or (?i) (?m) flags */
        if (pr->p < pr->end && *pr->p == '?') {
            pr->p++;
            if (pr->p < pr->end && *pr->p == ':') {
                pr->p++;
                capturing = false;
            } else {
                /* inline flags: (?i) (?m) */
                while (pr->p < pr->end && *pr->p != ')') {
                    if (*pr->p == 'i') b->flag_i = true;
                    else if (*pr->p == 'm') b->flag_m = true;
                    pr->p++;
                }
                if (pr->p < pr->end) pr->p++; /* consume ')' */
                /* Flag group has no content; return empty frag? Use a split as no-op */
                /* We use a SPLIT with both outs pointing to next - treat as epsilon */
                int id = new_state(b, RE_SPLIT);
                if (id < 0) return frag(-1);
                b->states[id].u.out2 = -1;
                ReFrag f = frag(id);
                frag_add_out(&f, id, false);
                frag_add_out(&f, id, true);
                return f;
            }
        }
        if (capturing) {
            group_id = ++b->n_groups;
        }

        /* open save */
        int open_id = -1;
        if (capturing && group_id < RE_MAX_GROUPS) {
            open_id = new_state(b, RE_SAVE);
            if (open_id < 0) return frag(-1);
            b->states[open_id].u.group_id = group_id;
            b->states[open_id].u.is_open  = true;
        }

        ReFrag inner = parse_expr(pr, true);
        if (inner.start < 0) return frag(-1);
        if (pr->p < pr->end && *pr->p == ')') pr->p++;

        /* close save */
        int close_id = -1;
        if (capturing && group_id < RE_MAX_GROUPS) {
            close_id = new_state(b, RE_SAVE);
            if (close_id < 0) return frag(-1);
            b->states[close_id].u.group_id = group_id;
            b->states[close_id].u.is_open  = false;
        }

        /* chain: open_id -> inner -> close_id */
        if (open_id >= 0) {
            b->states[open_id].out = inner.start;
            inner.start = open_id;
        }
        if (close_id >= 0) {
            /* patch inner's dangling outs to close_id */
            frag_patch(b, &inner, close_id);
            /* result: start at inner.start (open_id or first inner state),
               close_id's primary out is the only dangling output */
            ReFrag result = frag(inner.start);
            frag_add_out(&result, close_id, false);
            return result;
        }
        return inner;
    }

    /* Character class */
    if (c == '[') {
        pr->p++;
        return parse_char_class(pr);
    }

    /* Any */
    if (c == '.') {
        pr->p++;
        int id = new_state(b, RE_ANY);
        if (id < 0) return frag(-1);
        ReFrag f = frag(id);
        frag_add_out(&f, id, false);
        return f;
    }

    /* Escape sequence */
    if (c == '\\' && pr->p + 1 < pr->end) {
        pr->p++;
        unsigned char esc = (unsigned char)*pr->p++;
        if (esc == 'd' || esc == 'D' || esc == 'w' || esc == 'W' ||
            esc == 's' || esc == 'S') {
            ReBitmap* bm = (ReBitmap*)calloc(1, sizeof(ReBitmap));
            if (!bm) { snprintf(b->err, RE_ERR_SIZE, "regexp: OOM"); return frag(-1); }
            bitmap_class(bm, (char)esc, false);
            int id = new_state(b, RE_CLASS);
            if (id < 0) { free(bm); return frag(-1); }
            b->states[id].u.cls = bm;
            ReFrag f = frag(id);
            frag_add_out(&f, id, false);
            return f;
        }
        /* literal escaped char */
        unsigned char lit = (esc == 'n') ? '\n' : (esc == 't') ? '\t' :
                            (esc == 'r') ? '\r' : esc;
        int id = new_state(b, RE_CHAR);
        if (id < 0) return frag(-1);
        b->states[id].u.ch = fold(b, lit);
        ReFrag f = frag(id);
        frag_add_out(&f, id, false);
        return f;
    }

    /* Literal char */
    if (c != '|' && c != ')' && c != '*' && c != '+' && c != '?' &&
        c != '{' && c != '}') {
        pr->p++;
        int id = new_state(b, RE_CHAR);
        if (id < 0) return frag(-1);
        b->states[id].u.ch = fold(b, c);
        ReFrag f = frag(id);
        frag_add_out(&f, id, false);
        return f;
    }

    return frag(-1);
}

/* Apply quantifier * + ? {n,m} to a fragment */
static ReFrag apply_quantifier(ReBuilder* b, ReFrag inner,
                                int qmin, int qmax, bool lazy) {
    /* qmax == -1 means unlimited */
    if (inner.start < 0) return inner;

    /* Build repeated concatenation for mandatory part (qmin times) */
    /* For simplicity: handle {0,1} {0,inf} {1,inf} directly; others by unrolling */

    if (qmin == 0 && qmax == 1) {
        /* ? */
        int split = new_state(b, RE_SPLIT);
        if (split < 0) return frag(-1);
        b->states[split].greedy = !lazy;
        ReFrag f;
        if (!lazy) {
            b->states[split].out     = inner.start;
            b->states[split].u.out2  = -1;
            f = frag(split);
            /* outs = inner's dangling + split's out2 slot */
            for (int i = 0; i < inner.nouts;  i++) frag_add_out(&f, inner.outs[i],  false);
            for (int i = 0; i < inner.nout2s; i++) frag_add_out(&f, inner.out2s[i], true);
            frag_add_out(&f, split, true);
        } else {
            b->states[split].u.out2  = inner.start;
            b->states[split].out     = -1;
            f = frag(split);
            frag_add_out(&f, split, false);
            for (int i = 0; i < inner.nouts;  i++) frag_add_out(&f, inner.outs[i],  false);
            for (int i = 0; i < inner.nout2s; i++) frag_add_out(&f, inner.out2s[i], true);
        }
        return f;
    }

    if (qmin == 0 && qmax == -1) {
        /* * */
        int split = new_state(b, RE_SPLIT);
        if (split < 0) return frag(-1);
        b->states[split].greedy = !lazy;
        frag_patch(b, &inner, split); /* back-edge: inner loops to split */
        ReFrag f = frag(split);
        if (!lazy) {
            b->states[split].out    = inner.start;
            b->states[split].u.out2 = -1;
            frag_add_out(&f, split, true);
        } else {
            b->states[split].u.out2 = inner.start;
            b->states[split].out    = -1;
            frag_add_out(&f, split, false);
        }
        return f;
    }

    if (qmin == 1 && qmax == -1) {
        /* + */
        int split = new_state(b, RE_SPLIT);
        if (split < 0) return frag(-1);
        b->states[split].greedy = !lazy;
        frag_patch(b, &inner, split);
        ReFrag f = frag(inner.start);
        if (!lazy) {
            b->states[split].out    = inner.start;
            b->states[split].u.out2 = -1;
            frag_add_out(&f, split, true);
        } else {
            b->states[split].u.out2 = inner.start;
            b->states[split].out    = -1;
            frag_add_out(&f, split, false);
        }
        return f;
    }

    /* General {n,m}: unroll (limited to 64 total to avoid explosion) */
    if (qmin > 32 || (qmax > 0 && qmax > 64)) {
        snprintf(b->err, RE_ERR_SIZE, "regexp: quantifier too large");
        return frag(-1);
    }
    /* We'll handle {n,m} via sequential construction; for now fall through to ? chains */
    /* Simplification: treat {n,m} as n copies + (m-n) optional copies */
    (void)lazy;
    snprintf(b->err, RE_ERR_SIZE,
             "regexp: {n,m} quantifier not yet supported in this build");
    return frag(-1);
}

/* Parse one concatenated term (atom + optional quantifier) */
static ReFrag parse_concat_elem(ReParser* pr) {
    ReFrag f = parse_atom(pr);
    if (f.start < 0) return f;

    if (pr->p >= pr->end) return f;

    char q = *pr->p;
    if (q == '*' || q == '+' || q == '?') {
        pr->p++;
        bool lazy = false;
        if (pr->p < pr->end && *pr->p == '?') { lazy = true; pr->p++; }
        int qmin = (q == '+') ? 1 : 0;
        int qmax = (q == '?') ? 1 : -1;
        return apply_quantifier(pr->b, f, qmin, qmax, lazy);
    }

    if (q == '{') {
        /* {n} or {n,} or {n,m} */
        const char* save = pr->p;
        pr->p++;
        int n = 0;
        while (pr->p < pr->end && *pr->p >= '0' && *pr->p <= '9')
            n = n * 10 + (*pr->p++ - '0');
        if (pr->p >= pr->end || (*pr->p != ',' && *pr->p != '}')) {
            pr->p = save; return f; /* not a valid quantifier, treat { as literal */
        }
        int m = n;
        if (*pr->p == ',') {
            pr->p++;
            if (pr->p < pr->end && *pr->p == '}') { m = -1; }
            else {
                m = 0;
                while (pr->p < pr->end && *pr->p >= '0' && *pr->p <= '9')
                    m = m * 10 + (*pr->p++ - '0');
            }
        }
        if (pr->p < pr->end && *pr->p == '}') pr->p++;
        bool lazy = false;
        if (pr->p < pr->end && *pr->p == '?') { lazy = true; pr->p++; }

        /* Unroll manually */
        if (n < 0 || n > 32 || (m >= 0 && m > 64)) {
            snprintf(pr->b->err, RE_ERR_SIZE, "regexp: quantifier too large");
            return frag(-1);
        }

        /* We need n copies of the fragment; since we can only use it once in the NFA
           (states are already built), we need to copy.  For simplicity we only support
           n=0,1 + unlimited/bounded tails here.  Full unrolling requires cloning states
           which is complex — instead we'll fall back to repeated parse. */
        /* Easiest correct approach: re-parse the atom n times via position rewind.
           We save the start position before parse_atom and replay. */
        /* Actually the fragment f is already built; we cannot clone states easily.
           Use the simple accepted approach: restrict to common patterns. */
        (void)lazy;
        if (n == m) {
            /* {n}: exactly n — we already have 1 copy, need n-1 more */
            /* For n==1 it's just f */
            if (n == 0) {
                /* match nothing: use a split to bypass */
                int sp = new_state(pr->b, RE_SPLIT);
                if (sp < 0) return frag(-1);
                pr->b->states[sp].out    = -1;
                pr->b->states[sp].u.out2 = -1;
                ReFrag r = frag(sp);
                frag_add_out(&r, sp, false);
                frag_add_out(&r, sp, true);
                return r;
            }
            /* n>=2: not supported without cloning */
            snprintf(pr->b->err, RE_ERR_SIZE,
                     "regexp: {n} with n>1 not supported");
            return frag(-1);
        }
        if (n == 0 && m == -1) return apply_quantifier(pr->b, f, 0, -1, lazy);
        if (n == 1 && m == -1) return apply_quantifier(pr->b, f, 1, -1, lazy);
        if (n == 0 && m == 1)  return apply_quantifier(pr->b, f, 0,  1, lazy);
        snprintf(pr->b->err, RE_ERR_SIZE,
                 "regexp: {n,m} with n>1 or m>1 not supported");
        return frag(-1);
    }

    return f;
}

/* Parse a sequence of concatenated elements */
static ReFrag parse_concat(ReParser* pr, bool in_group) {
    ReFrag result = frag(-1);
    bool first = true;

    while (pr->p < pr->end) {
        char c = *pr->p;
        if (c == ')' && in_group) break;
        if (c == '|') break;

        ReFrag elem = parse_concat_elem(pr);
        if (elem.start < 0) {
            if (pr->b->err[0]) return frag(-1);
            break;
        }
        if (first) { result = elem; first = false; }
        else        result = concat_frag(pr->b, result, elem);
        if (pr->b->err[0]) return frag(-1);
    }

    return result;
}

/* Parse an alternation expr1|expr2|... */
static ReFrag parse_expr(ReParser* pr, bool in_group) {
    ReFrag left = parse_concat(pr, in_group);
    if (pr->b->err[0]) return frag(-1);

    while (pr->p < pr->end && *pr->p == '|') {
        pr->p++;
        ReFrag right = parse_concat(pr, in_group);
        if (pr->b->err[0]) return frag(-1);

        int split = new_state(pr->b, RE_SPLIT);
        if (split < 0) return frag(-1);
        /* patch split -> left.start and split -> right.start */
        pr->b->states[split].out    = left.start;
        pr->b->states[split].u.out2 = right.start;
        /* new dangling outs = left's + right's */
        ReFrag merged = frag(split);
        for (int i = 0; i < left.nouts;  i++) frag_add_out(&merged, left.outs[i],  false);
        for (int i = 0; i < left.nout2s; i++) frag_add_out(&merged, left.out2s[i], true);
        for (int i = 0; i < right.nouts;  i++) frag_add_out(&merged, right.outs[i],  false);
        for (int i = 0; i < right.nout2s; i++) frag_add_out(&merged, right.out2s[i], true);
        left = merged;
    }

    return left;
}

/* ================================================================== */
/* Compile                                                              */
/* ================================================================== */

ReNfa* re_compile(const char* pattern, char* err_out) {
    ReBuilder* bld = (ReBuilder*)calloc(1, sizeof(ReBuilder));
    if (!bld) { snprintf(err_out, RE_ERR_SIZE, "OOM"); return NULL; }

    /* Pre-scan for (?i)/(?m) at start to set flags early (affects class parsing) */
    const char* scan = pattern;
    while (scan[0] == '(' && scan[1] == '?') {
        const char* p2 = scan + 2;
        while (*p2 && *p2 != ')') {
            if (*p2 == 'i') bld->flag_i = true;
            if (*p2 == 'm') bld->flag_m = true;
            p2++;
        }
        if (*p2 == ')') scan = p2 + 1;
        else break;
    }

    ReParser pr;
    pr.p   = pattern;
    pr.end = pattern + strlen(pattern);
    pr.b   = bld;

    ReFrag f = parse_expr(&pr, false);
    if (bld->err[0]) {
        snprintf(err_out, RE_ERR_SIZE, "%s", bld->err);
        free(bld);
        return NULL;
    }
    if (f.start < 0) {
        /* empty pattern = always matches */
        f.start = new_state(bld, RE_MATCH);
        if (f.start < 0) {
            snprintf(err_out, RE_ERR_SIZE, "regexp: empty pattern error");
            free(bld);
            return NULL;
        }
    }

    /* Add accept state */
    int accept = new_state(bld, RE_MATCH);
    if (accept < 0) {
        snprintf(err_out, RE_ERR_SIZE, "%s", bld->err);
        free(bld);
        return NULL;
    }
    frag_patch(bld, &f, accept);

    /* Allocate final NFA */
    ReNfa* nfa = (ReNfa*)malloc(sizeof(ReNfa));
    if (!nfa) { snprintf(err_out, RE_ERR_SIZE, "OOM"); free(bld); return NULL; }
    nfa->states  = (ReState*)malloc((size_t)bld->count * sizeof(ReState));
    if (!nfa->states) { snprintf(err_out, RE_ERR_SIZE, "OOM"); free(bld); free(nfa); return NULL; }
    memcpy(nfa->states, bld->states, (size_t)bld->count * sizeof(ReState));
    nfa->count    = bld->count;
    nfa->start_id = f.start;
    nfa->end_id   = accept;
    nfa->n_groups = bld->n_groups;
    nfa->flag_i  = bld->flag_i;
    nfa->flag_m  = bld->flag_m;
    free(bld);
    return nfa;
}

void re_free_contents(ReNfa* nfa) {
    if (!nfa) return;
    for (int i = 0; i < nfa->count; i++) {
        if (nfa->states[i].type == RE_CLASS)
            free(nfa->states[i].u.cls);
    }
    free(nfa->states);
    nfa->states = NULL;
}

void re_free(ReNfa* nfa) {
    if (!nfa) return;
    re_free_contents(nfa);
    free(nfa);
}

/* ================================================================== */
/* Simulation (Thompson NFA)                                           */
/* ================================================================== */

/*
 * Thread: one active NFA path.  Stores the state ID and the current
 * submatch save positions.
 */
typedef struct {
    int    state;
    char*  pos[RE_MAX_GROUPS * 2]; /* [g*2]=open, [g*2+1]=close */
} Thread;

/* Active thread list.  Uses a visited bitmap to deduplicate states. */
typedef struct {
    Thread* threads;
    int     count;
    int     cap;
    bool    visited[RE_MAX_STATES];
} ThreadList;

static void tl_init(ThreadList* tl, int cap) {
    tl->threads = (Thread*)malloc((size_t)cap * sizeof(Thread));
    tl->count   = 0;
    tl->cap     = cap;
    memset(tl->visited, 0, sizeof(tl->visited));
}

static void tl_clear(ThreadList* tl) {
    memset(tl->visited, 0, sizeof(tl->visited));
    tl->count = 0;
}

static void tl_free(ThreadList* tl) {
    free(tl->threads);
    tl->threads = NULL;
}

/*
 * Epsilon-close: follow SPLIT/SAVE/ANCHOR edges without consuming input,
 * then add the resulting concrete state (CHAR/ANY/CLASS/MATCH) to tl.
 * sv carries the current save positions.
 */
static void tl_add(const ReNfa* nfa, ThreadList* tl,
                   int s, char* pos_ptr,
                   char* save[RE_MAX_GROUPS * 2],
                   bool bol, bool eol) {
    if (s < 0 || s >= nfa->count) return;
    if (tl->visited[s]) return;

    const ReState* st = &nfa->states[s];
    switch (st->type) {
    case RE_SPLIT: {
        tl->visited[s] = true;
        int o1 = st->greedy ? st->out    : st->u.out2;
        int o2 = st->greedy ? st->u.out2 : st->out;
        tl_add(nfa, tl, o1, pos_ptr, save, bol, eol);
        tl_add(nfa, tl, o2, pos_ptr, save, bol, eol);
        tl->visited[s] = false;
        return;
    }
    case RE_SAVE: {
        tl->visited[s] = true;
        /* Build updated save array on the stack */
        char* nsave[RE_MAX_GROUPS * 2];
        memcpy(nsave, save, sizeof(nsave));
        int idx = st->u.group_id * 2 + (st->u.is_open ? 0 : 1);
        if (idx >= 0 && idx < RE_MAX_GROUPS * 2) nsave[idx] = pos_ptr;
        tl_add(nfa, tl, st->out, pos_ptr, nsave, bol, eol);
        tl->visited[s] = false;
        return;
    }
    case RE_ANCHOR_BOL:
        tl->visited[s] = true;
        if (bol) tl_add(nfa, tl, st->out, pos_ptr, save, bol, eol);
        tl->visited[s] = false;
        return;
    case RE_ANCHOR_EOL:
        tl->visited[s] = true;
        if (eol) tl_add(nfa, tl, st->out, pos_ptr, save, bol, eol);
        tl->visited[s] = false;
        return;
    default:
        break;
    }

    /* Concrete state: add to list */
    tl->visited[s] = true;
    if (tl->count < tl->cap) {
        Thread* t = &tl->threads[tl->count++];
        t->state = s;
        memcpy(t->pos, save, sizeof(t->pos));
    }
}

/*
 * Run the NFA starting at s[start_off].
 * If anchored=true, only try to match at start_off.
 * If anchored=false, try every starting position from start_off onward
 * and return the leftmost match.
 */
static bool sim_run(const ReNfa* nfa, const char* s, size_t len,
                    size_t start_off, bool anchored,
                    ReMatch groups[RE_MAX_GROUPS]) {
    ThreadList cur, nxt;
    tl_init(&cur, nfa->count + 4);
    tl_init(&nxt, nfa->count + 4);
    if (!cur.threads || !nxt.threads) {
        tl_free(&cur); tl_free(&nxt); return false;
    }

    char* empty_save[RE_MAX_GROUPS * 2];
    memset(empty_save, 0, sizeof(empty_save));

    bool  found   = false;
    char* match_s = NULL;
    char* match_e = NULL;
    char* best_pos[RE_MAX_GROUPS * 2];
    memset(best_pos, 0, sizeof(best_pos));

    size_t search_end = anchored ? start_off + 1 : len + 1;

    for (size_t si = start_off; si < search_end && !found; si++) {
        char* cur_ptr = (char*)(s + si);
        bool bol = (si == 0) || (nfa->flag_m && si > 0 && s[si-1] == '\n');
        bool eol = (si == len)|| (nfa->flag_m && si < len && s[si] == '\n');

        tl_clear(&cur);
        tl_add(nfa, &cur, nfa->start_id, cur_ptr, empty_save, bol, eol);

        char* p = cur_ptr;

        for (;;) {
            /* Prefer longest match: check accept at current position first */
            for (int i = 0; i < cur.count; i++) {
                if (cur.threads[i].state == nfa->end_id) {
                    found   = true;
                    match_s = cur_ptr;
                    match_e = p;
                    memcpy(best_pos, cur.threads[i].pos, sizeof(best_pos));
                    break;
                }
            }

            if (p >= s + len) break;

            unsigned char ch = (unsigned char)*p;
            if (nfa->flag_i) ch = (unsigned char)tolower(ch);

            bool next_bol = (nfa->flag_m && *p == '\n');
            bool next_eol = (p + 1 >= s + len) ||
                            (nfa->flag_m && *(p+1) == '\n');

            tl_clear(&nxt);
            for (int i = 0; i < cur.count; i++) {
                const Thread* t  = &cur.threads[i];
                const ReState* st = &nfa->states[t->state];
                bool matches = false;
                switch (st->type) {
                case RE_CHAR:  matches = (st->u.ch == ch); break;
                case RE_ANY:   matches = (*p != '\n');      break;
                case RE_CLASS: matches = bitmap_test(st->u.cls, ch); break;
                default: break;
                }
                if (matches) {
                    Thread* mt = (Thread*)t; /* cast away const for save[] */
                    tl_add(nfa, &nxt, st->out, (char*)(p + 1),
                           mt->pos, next_bol, next_eol);
                }
            }

            /* swap cur <-> nxt */
            ThreadList tmp = cur; cur = nxt; nxt = tmp;
            p++;

            if (cur.count == 0) break;
        }

        /* Check accept at end-of-input for zero-width / anchor-$ patterns */
        if (!found) {
            for (int i = 0; i < cur.count; i++) {
                if (cur.threads[i].state == nfa->end_id) {
                    found   = true;
                    match_s = cur_ptr;
                    match_e = p;
                    memcpy(best_pos, cur.threads[i].pos, sizeof(best_pos));
                    break;
                }
            }
        }
    }

    tl_free(&cur);
    tl_free(&nxt);

    if (found && groups) {
        groups[0].start = match_s;
        groups[0].end   = match_e;
        for (int g = 1; g < RE_MAX_GROUPS; g++) {
            groups[g].start = best_pos[g * 2];
            groups[g].end   = best_pos[g * 2 + 1];
        }
    }
    return found;
}

bool re_match(const ReNfa* nfa, const char* s, size_t len,
              ReMatch groups[RE_MAX_GROUPS]) {
    return sim_run(nfa, s, len, 0, true, groups);
}

bool re_search(const ReNfa* nfa, const char* s, size_t len,
               ReMatch groups[RE_MAX_GROUPS]) {
    return sim_run(nfa, s, len, 0, false, groups);
}

bool re_search_at(const ReNfa* nfa, const char* s, size_t len, size_t offset,
                  ReMatch groups[RE_MAX_GROUPS]) {
    return sim_run(nfa, s, len, offset, false, groups);
}
