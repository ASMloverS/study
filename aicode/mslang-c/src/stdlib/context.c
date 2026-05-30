/* src/stdlib/context.c - STDLIB-42: context module
 *
 * Request context: cancellation signal, deadline, key-value propagation.
 * Modelled on Go's context package.
 *
 * Public API:
 *   context.background()               → Context (root, never cancels)
 *   context.with_cancel(parent)        → Context  + ctx.cancel()
 *   context.with_timeout(parent, ms)   → Context  (auto-cancel after ms ms)
 *   context.with_deadline(parent, ts)  → Context  (auto-cancel at ts ms epoch)
 *   context.with_value(parent, k, v)   → Context  (carries k→v, no cancel)
 *
 * Instance methods (OOP dispatch via ms_context_invoke):
 *   ctx.cancel()       → nil    cancel this ctx (idempotent, propagates to children)
 *   ctx.is_cancelled() → bool   true if this or any ancestor is cancelled
 *   ctx.deadline()     → int|nil  deadline timestamp (ms since epoch) or nil
 *   ctx.value(key)     → any|nil  key lookup (walks parent chain)
 *   ctx.done()         → Future  resolves when ctx is cancelled
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/memory.h"
#include "ms/event_loop.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Context userdata                                                    */
/* ================================================================== */

#define CTX_TAG "context.Context"

typedef struct MsContext MsContext;

/* Simple key-value pair list for with_value chains */
typedef struct KVNode {
    MsValue    key;
    MsValue    val;
    struct KVNode* next;
} KVNode;

/* Child list: contexts cancel all registered children when they cancel */
typedef struct ChildNode {
    MsContext*      child;
    struct ChildNode* next;
} ChildNode;

/* Waiter list: futures to resolve on cancel (done() futures) */
typedef struct DoneNode {
    MsObjFuture*   fut;
    struct DoneNode* next;
} DoneNode;

struct MsContext {
    bool         cancelled;
    ms_i64       deadline;   /* 0 = no deadline */
    MsContext*   parent;     /* NULL for root */
    KVNode*      kv;         /* own key-value (with_value only) */
    ChildNode*   children;   /* children to cancel */
    DoneNode*    done_waiters;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void ensure_loop(MsVM* vm) {
    if (!vm->loop_inited) {
        ms_loop_init(&vm->event_loop, vm);
        vm->loop_inited = true;
    }
}

static MsContext* ctx_alloc(void) {
    MsContext* c = (MsContext*)calloc(1, sizeof(MsContext));
    return c;
}

/* Free only internal resources; the MsContext* itself is freed by ms_object_free
   after calling ctx_finalize. */
static void ctx_free_internals(MsContext* c) {
    /* free kv chain */
    KVNode* kv = c->kv;
    while (kv) {
        KVNode* nx = kv->next;
        free(kv);
        kv = nx;
    }
    c->kv = NULL;
    /* free child list nodes (not the children themselves — GC owns them) */
    ChildNode* ch = c->children;
    while (ch) {
        ChildNode* nx = ch->next;
        free(ch);
        ch = nx;
    }
    c->children = NULL;
    /* free done_waiters list nodes */
    DoneNode* dn = c->done_waiters;
    while (dn) {
        DoneNode* nx = dn->next;
        free(dn);
        dn = nx;
    }
    c->done_waiters = NULL;
}

/* GC mark: mark kv values and done futures */
static void ctx_finalize(void* d) {
    ctx_free_internals((MsContext*)d);
    /* NOTE: do NOT free(d) here — ms_object_free calls free(ud->data) after us */
}

static void ctx_mark(MsVM* vm, void* d) {
    MsContext* c = (MsContext*)d;
    for (KVNode* kv = c->kv; kv; kv = kv->next) {
        ms_mark_value(vm, kv->key);
        ms_mark_value(vm, kv->val);
    }
    for (DoneNode* dn = c->done_waiters; dn; dn = dn->next) {
        ms_mark_object(vm, (MsObject*)dn->fut);
    }
}

/* Create a new context userdata wrapping MsContext* c */
static MsValue ctx_wrap(MsVM* vm, MsContext* c) {
    MsObjUserdata* ud = ms_obj_userdata_new(vm, 0, ctx_finalize, ctx_mark, CTX_TAG);
    ud->data      = c;
    ud->data_size = sizeof(MsContext);
    return MS_OBJ_VAL((MsObject*)ud);
}

/* Extract MsContext* from a MsValue (returns NULL if wrong type) */
static MsContext* ctx_unwrap(MsValue v) {
    if (!MS_IS_USERDATA(v)) return NULL;
    MsObjUserdata* ud = MS_AS_USERDATA(v);
    if (strcmp(ud->type_tag, CTX_TAG) != 0) return NULL;
    return (MsContext*)ud->data;
}

/* Register child under parent so cancel() propagates */
static void ctx_add_child(MsContext* parent, MsContext* child) {
    ChildNode* node = (ChildNode*)malloc(sizeof(ChildNode));
    if (!node) return;
    node->child = child;
    node->next  = parent->children;
    parent->children = node;
}

/* Cancel this context and all children; resolve done futures */
static void ctx_cancel(MsVM* vm, MsContext* c) {
    if (c->cancelled) return;
    c->cancelled = true;
    /* resolve done() futures */
    for (DoneNode* dn = c->done_waiters; dn; dn = dn->next) {
        if (dn->fut->state == MS_FUTURE_PENDING)
            ms_future_resolve(vm, dn->fut, MS_NIL_VAL());
    }
    /* propagate to children */
    for (ChildNode* ch = c->children; ch; ch = ch->next) {
        ctx_cancel(vm, ch->child);
    }
}

/* Check cancellation: walk parent chain */
static bool ctx_is_cancelled(MsContext* c) {
    while (c) {
        if (c->cancelled) return true;
        c = c->parent;
    }
    return false;
}

/* Lookup key: walk parent chain */
static bool ctx_value(MsContext* c, MsValue key, MsValue* out) {
    while (c) {
        for (KVNode* kv = c->kv; kv; kv = kv->next) {
            /* Compare by identity / numeric equality */
            if (MS_IS_STRING(kv->key) && MS_IS_STRING(key)) {
                MsObjString* a = MS_AS_STRING(kv->key);
                MsObjString* b = MS_AS_STRING(key);
                if (a == b || (a->length == b->length &&
                               memcmp(a->data, b->data, (size_t)a->length) == 0)) {
                    *out = kv->val;
                    return true;
                }
            } else if (MS_IS_INT(kv->key) && MS_IS_INT(key)) {
                if (MS_AS_INT(kv->key) == MS_AS_INT(key)) {
                    *out = kv->val;
                    return true;
                }
            } else if (MS_IS_NUMBER(kv->key) && MS_IS_NUMBER(key)) {
                if (MS_AS_NUMBER(kv->key) == MS_AS_NUMBER(key)) {
                    *out = kv->val;
                    return true;
                }
            }
        }
        c = c->parent;
    }
    return false;
}

/* ================================================================== */
/* Timer callback for with_timeout / with_deadline                    */
/* ================================================================== */

typedef struct {
    MsContext* ctx;
} TimerUD;

static void timer_on_resolve(MsVM* vm, void* ud, int idx, MsValue result) {
    (void)idx; (void)result;
    TimerUD* tu = (TimerUD*)ud;
    ctx_cancel(vm, tu->ctx);
    free(tu);
}
static void timer_on_reject(MsVM* vm, void* ud, MsValue error) {
    (void)vm; (void)error;
    TimerUD* tu = (TimerUD*)ud;
    free(tu);
}

/* ================================================================== */
/* Module functions                                                    */
/* ================================================================== */

static MsValue context_background(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    MsContext* c = ctx_alloc();
    if (!c) { ms_vm_runtime_error(vm, "context.background: OOM"); return MS_NIL_VAL(); }
    return ctx_wrap(vm, c);
}

static MsValue context_with_cancel(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) {
        ms_vm_runtime_error(vm, "context.with_cancel: expected parent context");
        return MS_NIL_VAL();
    }
    MsContext* parent = ctx_unwrap(argv[0]);
    if (!parent) {
        ms_vm_runtime_error(vm, "context.with_cancel: argument must be a Context");
        return MS_NIL_VAL();
    }
    MsContext* c = ctx_alloc();
    if (!c) { ms_vm_runtime_error(vm, "context.with_cancel: OOM"); return MS_NIL_VAL(); }
    c->parent   = parent;
    c->deadline = parent->deadline; /* inherit deadline */
    ctx_add_child(parent, c);
    return ctx_wrap(vm, c);
}

static MsValue context_with_timeout(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "context.with_timeout: expected (parent, ms)");
        return MS_NIL_VAL();
    }
    MsContext* parent = ctx_unwrap(argv[0]);
    if (!parent) {
        ms_vm_runtime_error(vm, "context.with_timeout: first argument must be a Context");
        return MS_NIL_VAL();
    }
    ms_i64 delay = 0;
    if (MS_IS_INT(argv[1]))    delay = MS_AS_INT(argv[1]);
    else if (MS_IS_NUMBER(argv[1])) delay = (ms_i64)MS_AS_NUMBER(argv[1]);
    if (delay < 0) delay = 0;

    ms_i64 now_ms = (ms_i64)ms_monotonic_ms();
    ms_i64 deadline = now_ms + delay;

    MsContext* c = ctx_alloc();
    if (!c) { ms_vm_runtime_error(vm, "context.with_timeout: OOM"); return MS_NIL_VAL(); }
    c->parent   = parent;
    c->deadline = deadline;
    if (parent->deadline > 0 && parent->deadline < deadline)
        c->deadline = parent->deadline;
    ctx_add_child(parent, c);

    MsValue ctx_val = ctx_wrap(vm, c);

    /* Schedule the timer: create a sleep future, then cancel ctx when it fires */
    ensure_loop(vm);
    MsObjFuture* timer_fut = ms_obj_future_new(vm);
    ms_loop_call_later(&vm->event_loop, (uint64_t)delay, timer_fut);
    TimerUD* tu = (TimerUD*)malloc(sizeof(TimerUD));
    if (tu) {
        tu->ctx = c;
        ms_future_add_cb_waiter(vm, timer_fut, timer_on_resolve, timer_on_reject, tu, 0);
    }
    return ctx_val;
}

static MsValue context_with_deadline(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "context.with_deadline: expected (parent, ts)");
        return MS_NIL_VAL();
    }
    MsContext* parent = ctx_unwrap(argv[0]);
    if (!parent) {
        ms_vm_runtime_error(vm, "context.with_deadline: first argument must be a Context");
        return MS_NIL_VAL();
    }
    ms_i64 deadline_ts = 0;
    if (MS_IS_INT(argv[1]))    deadline_ts = MS_AS_INT(argv[1]);
    else if (MS_IS_NUMBER(argv[1])) deadline_ts = (ms_i64)MS_AS_NUMBER(argv[1]);

    MsContext* c = ctx_alloc();
    if (!c) { ms_vm_runtime_error(vm, "context.with_deadline: OOM"); return MS_NIL_VAL(); }
    c->parent   = parent;
    c->deadline = deadline_ts;
    if (parent->deadline > 0 && parent->deadline < deadline_ts)
        c->deadline = parent->deadline;
    ctx_add_child(parent, c);

    MsValue ctx_val = ctx_wrap(vm, c);

    /* Compute delay from now */
    ms_i64 now_ms = (ms_i64)ms_monotonic_ms();
    ms_i64 delay  = c->deadline - now_ms;
    if (delay <= 0) {
        ctx_cancel(vm, c);
    } else {
        ensure_loop(vm);
        MsObjFuture* timer_fut = ms_obj_future_new(vm);
        ms_loop_call_later(&vm->event_loop, (uint64_t)delay, timer_fut);
        TimerUD* tu = (TimerUD*)malloc(sizeof(TimerUD));
        if (tu) {
            tu->ctx = c;
            ms_future_add_cb_waiter(vm, timer_fut, timer_on_resolve, timer_on_reject, tu, 0);
        }
    }
    return ctx_val;
}

static MsValue context_with_value(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 3) {
        ms_vm_runtime_error(vm, "context.with_value: expected (parent, key, val)");
        return MS_NIL_VAL();
    }
    MsContext* parent = ctx_unwrap(argv[0]);
    if (!parent) {
        ms_vm_runtime_error(vm, "context.with_value: first argument must be a Context");
        return MS_NIL_VAL();
    }
    MsContext* c = ctx_alloc();
    if (!c) { ms_vm_runtime_error(vm, "context.with_value: OOM"); return MS_NIL_VAL(); }
    c->parent = parent;
    c->deadline = parent->deadline;

    KVNode* kv = (KVNode*)malloc(sizeof(KVNode));
    if (!kv) { ctx_free_internals(c); free(c); ms_vm_runtime_error(vm, "context.with_value: OOM"); return MS_NIL_VAL(); }
    kv->key  = argv[1];
    kv->val  = argv[2];
    kv->next = NULL;
    c->kv    = kv;

    /* with_value contexts are not cancellable, but still registered
       as children so parent cancellation propagates */
    ctx_add_child(parent, c);

    return ctx_wrap(vm, c);
}

/* ================================================================== */
/* Method dispatch (called from ms_builtin_invoke in vm_builtins.c)  */
/* ================================================================== */

bool ms_context_invoke(MsVM* vm, MsObjUserdata* ud,
                       MsObjString* method, int argc, MsValue* argv,
                       MsValue* out) {
    if (strcmp(ud->type_tag, CTX_TAG) != 0) return false;
    MsContext* c = (MsContext*)ud->data;
    const char* m = method->data;

    if (strcmp(m, "cancel") == 0) {
        ctx_cancel(vm, c);
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(m, "is_cancelled") == 0) {
        *out = MS_BOOL_VAL(ctx_is_cancelled(c));
        return true;
    }
    if (strcmp(m, "deadline") == 0) {
        if (c->deadline == 0)
            *out = MS_NIL_VAL();
        else
            *out = MS_INT_VAL(c->deadline);
        return true;
    }
    if (strcmp(m, "value") == 0) {
        MsValue key = (argc >= 1) ? argv[0] : MS_NIL_VAL();
        MsValue found;
        if (ctx_value(c, key, &found))
            *out = found;
        else
            *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(m, "done") == 0) {
        /* Return a Future that resolves when the context is cancelled */
        MsObjFuture* fut = ms_obj_future_new(vm);
        if (ctx_is_cancelled(c)) {
            ms_future_resolve(vm, fut, MS_NIL_VAL());
        } else {
            DoneNode* dn = (DoneNode*)malloc(sizeof(DoneNode));
            if (dn) {
                dn->fut  = fut;
                dn->next = c->done_waiters;
                c->done_waiters = dn;
            }
        }
        *out = MS_OBJ_VAL((MsObject*)fut);
        return true;
    }

    return false;
}

/* ================================================================== */
/* Module init                                                         */
/* ================================================================== */

static const MsNativeDef context_natives[] = {
    {"background",     context_background,     0},
    {"with_cancel",    context_with_cancel,     1},
    {"with_timeout",   context_with_timeout,    2},
    {"with_deadline",  context_with_deadline,   2},
    {"with_value",     context_with_value,      3},
    {NULL, NULL, 0}
};

void ms_module_context_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, context_natives);
}
