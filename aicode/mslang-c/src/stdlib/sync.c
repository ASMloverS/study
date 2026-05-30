/* src/stdlib/sync.c - STDLIB-41: sync module
 *
 * Coroutine-safe synchronisation primitives.
 * All types are implemented as C userdata to work safely from async coroutines.
 *
 * Public API:
 *   sync.new_once(fn)        → Once      (userdata)
 *   sync.new_wait_group()    → WaitGroup (userdata)
 *   sync.new_channel(cap)    → Channel   (userdata)
 *   sync.gather(futures)     → Future
 *   sync.race(futures)       → Future
 *
 * Once methods:     do()  done()
 * WaitGroup:        add(n)  done()  wait() → Future
 * Channel:          send(v) → Future   recv() → Future
 *                   close()  is_closed()  len()
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/memory.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================== */
/* Simple dynamic array of MsValue                                    */
/* ================================================================== */

typedef struct { MsValue* data; int count; int cap; } ValArr;

static void valarr_init(ValArr* a) { a->data = NULL; a->count = 0; a->cap = 0; }

static void valarr_free(ValArr* a) {
    free(a->data);
    valarr_init(a);
}

static bool valarr_push(ValArr* a, MsValue v) {
    if (a->count == a->cap) {
        int nc = (a->cap < 4) ? 4 : a->cap * 2;
        MsValue* d = (MsValue*)realloc(a->data, (size_t)nc * sizeof(MsValue));
        if (!d) return false;
        a->data = d;
        a->cap  = nc;
    }
    a->data[a->count++] = v;
    return true;
}

/* Remove and return element at index 0 (O(n) shift — queues are small). */
static MsValue valarr_shift(ValArr* a) {
    MsValue v = a->data[0];
    if (a->count > 1)
        memmove(a->data, a->data + 1, (size_t)(a->count - 1) * sizeof(MsValue));
    a->count--;
    return v;
}

static void valarr_mark(MsVM* vm, ValArr* a) {
    for (int i = 0; i < a->count; i++)
        ms_mark_value(vm, a->data[i]);
}

/* ================================================================== */
/* Once                                                               */
/* ================================================================== */

#define ONCE_TAG "sync.Once"

typedef struct { MsValue fn; bool done; } SyncOnce;

static void once_finalize(void* d) { (void)d; }
static void once_mark(MsVM* vm, void* d) {
    ms_mark_value(vm, ((SyncOnce*)d)->fn);
}

static MsValue sync_new_once(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) {
        ms_vm_runtime_error(vm, "sync.new_once: expected function argument");
        return MS_NIL_VAL();
    }
    SyncOnce* o = (SyncOnce*)malloc(sizeof(SyncOnce));
    if (!o) { ms_vm_runtime_error(vm, "sync.new_once: OOM"); return MS_NIL_VAL(); }
    o->fn   = argv[0];
    o->done = false;
    MsObjUserdata* ud = ms_obj_userdata_new(vm, 0, once_finalize, once_mark, ONCE_TAG);
    ud->data      = o;
    ud->data_size = sizeof(SyncOnce);
    return MS_OBJ_VAL((MsObject*)ud);
}

/* ================================================================== */
/* WaitGroup                                                          */
/* ================================================================== */

#define WG_TAG "sync.WaitGroup"

typedef struct { int count; ValArr waiters; } SyncWG;

static void wg_finalize(void* d) {
    SyncWG* w = (SyncWG*)d;
    valarr_free(&w->waiters);
}
static void wg_mark(MsVM* vm, void* d) {
    valarr_mark(vm, &((SyncWG*)d)->waiters);
}

static MsValue sync_new_wait_group(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    SyncWG* w = (SyncWG*)malloc(sizeof(SyncWG));
    if (!w) { ms_vm_runtime_error(vm, "sync.new_wait_group: OOM"); return MS_NIL_VAL(); }
    w->count = 0;
    valarr_init(&w->waiters);
    MsObjUserdata* ud = ms_obj_userdata_new(vm, 0, wg_finalize, wg_mark, WG_TAG);
    ud->data      = w;
    ud->data_size = sizeof(SyncWG);
    return MS_OBJ_VAL((MsObject*)ud);
}

/* ================================================================== */
/* Channel                                                            */
/* ================================================================== */

#define CH_TAG "sync.Channel"

/* send_q stores pairs: [future, value, future, value, ...] */
typedef struct {
    int    cap;
    bool   closed;
    ValArr buf;      /* buffered items                       */
    ValArr send_q;   /* parked senders: flat [fut,val,...]   */
    ValArr recv_q;   /* parked receivers: [fut,...]          */
} SyncChan;

static void chan_finalize(void* d) {
    SyncChan* c = (SyncChan*)d;
    valarr_free(&c->buf);
    valarr_free(&c->send_q);
    valarr_free(&c->recv_q);
}
static void chan_mark(MsVM* vm, void* d) {
    SyncChan* c = (SyncChan*)d;
    valarr_mark(vm, &c->buf);
    valarr_mark(vm, &c->send_q);
    valarr_mark(vm, &c->recv_q);
}

static MsValue sync_new_channel(MsVM* vm, int argc, MsValue* argv) {
    int cap = 0;
    if (argc >= 1 && MS_IS_INT(argv[0]))    cap = (int)MS_AS_INT(argv[0]);
    else if (argc >= 1 && MS_IS_NUMBER(argv[0])) cap = (int)MS_AS_NUMBER(argv[0]);
    if (cap < 0) cap = 0;

    SyncChan* c = (SyncChan*)malloc(sizeof(SyncChan));
    if (!c) { ms_vm_runtime_error(vm, "sync.new_channel: OOM"); return MS_NIL_VAL(); }
    c->cap    = cap;
    c->closed = false;
    valarr_init(&c->buf);
    valarr_init(&c->send_q);
    valarr_init(&c->recv_q);

    MsObjUserdata* ud = ms_obj_userdata_new(vm, 0, chan_finalize, chan_mark, CH_TAG);
    ud->data      = c;
    ud->data_size = sizeof(SyncChan);
    return MS_OBJ_VAL((MsObject*)ud);
}

/* ================================================================== */
/* sync.race — first future to resolve/reject wins                   */
/* ================================================================== */

typedef struct { MsObjFuture* parent; } RaceCtx;

static void race_resolve(MsVM* vm, void* ud, int idx, MsValue result) {
    (void)idx;
    RaceCtx* ctx = (RaceCtx*)ud;
    if (ctx->parent->state == MS_FUTURE_PENDING)
        ms_future_resolve(vm, ctx->parent, result);
}
static void race_reject(MsVM* vm, void* ud, MsValue error) {
    RaceCtx* ctx = (RaceCtx*)ud;
    if (ctx->parent->state == MS_FUTURE_PENDING)
        ms_future_reject(vm, ctx->parent, error);
}

static MsValue sync_race(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sync.race: expected a list of futures");
        return MS_NIL_VAL();
    }
    MsObjList*   list   = MS_AS_LIST(argv[0]);
    int          n      = list->items.count;
    MsObjFuture* parent = ms_obj_future_new(vm);

    if (n == 0) {
        ms_future_resolve(vm, parent, MS_NIL_VAL());
        return MS_OBJ_VAL((MsObject*)parent);
    }
    for (int i = 0; i < n && parent->state == MS_FUTURE_PENDING; i++) {
        MsValue v = list->items.data[i];
        if (!MS_IS_FUTURE(v)) {
            ms_vm_runtime_error(vm, "sync.race: all elements must be futures");
            return MS_NIL_VAL();
        }
        MsObjFuture* f = MS_AS_FUTURE(v);
        if (f->state == MS_FUTURE_RESOLVED) {
            ms_future_resolve(vm, parent, f->result);
        } else if (f->state == MS_FUTURE_REJECTED) {
            ms_future_reject(vm, parent, f->result);
        } else {
            RaceCtx* ctx = (RaceCtx*)malloc(sizeof(RaceCtx));
            if (!ctx) { ms_vm_runtime_error(vm, "sync.race: OOM"); return MS_NIL_VAL(); }
            ctx->parent = parent;
            ms_future_add_cb_waiter(vm, f, race_resolve, race_reject, ctx, i);
        }
    }
    return MS_OBJ_VAL((MsObject*)parent);
}

/* ================================================================== */
/* sync.gather — identical to time.gather; duplicate to avoid module  */
/* dependency inside native C code                                    */
/* ================================================================== */

typedef struct {
    MsObjFuture* parent;
    MsValue*     results;
    int          total;
    int          remaining;
} GatherCtx;

static void gather_resolve(MsVM* vm, void* ud, int idx, MsValue result) {
    GatherCtx* ctx = (GatherCtx*)ud;
    if (ctx->parent->state != MS_FUTURE_PENDING) return;
    ctx->results[idx] = result;
    if (--ctx->remaining == 0) {
        MsObjList* lst = ms_obj_list_from_array(vm, ctx->results, ctx->total);
        ms_future_resolve(vm, ctx->parent, MS_OBJ_VAL((MsObject*)lst));
        free(ctx->results);
        free(ctx);
    }
}
static void gather_reject(MsVM* vm, void* ud, MsValue error) {
    GatherCtx* ctx = (GatherCtx*)ud;
    if (ctx->parent->state != MS_FUTURE_PENDING) return;
    ms_future_reject(vm, ctx->parent, error);
}

static MsValue sync_gather(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sync.gather: expected a list of futures");
        return MS_NIL_VAL();
    }
    MsObjList*   list   = MS_AS_LIST(argv[0]);
    int          n      = list->items.count;
    MsObjFuture* parent = ms_obj_future_new(vm);

    if (n == 0) {
        MsObjList* empty = ms_obj_list_new(vm);
        ms_future_resolve(vm, parent, MS_OBJ_VAL((MsObject*)empty));
        return MS_OBJ_VAL((MsObject*)parent);
    }
    GatherCtx* ctx = (GatherCtx*)malloc(sizeof(GatherCtx));
    if (!ctx) { ms_vm_runtime_error(vm, "sync.gather: OOM"); return MS_NIL_VAL(); }
    ctx->results = (MsValue*)malloc(sizeof(MsValue) * (size_t)n);
    if (!ctx->results) {
        free(ctx);
        ms_vm_runtime_error(vm, "sync.gather: OOM");
        return MS_NIL_VAL();
    }
    ctx->parent    = parent;
    ctx->total     = n;
    ctx->remaining = n;
    for (int i = 0; i < n; i++) ctx->results[i] = MS_NIL_VAL();

    for (int i = 0; i < n; i++) {
        MsValue v = list->items.data[i];
        if (!MS_IS_FUTURE(v)) {
            free(ctx->results); free(ctx);
            ms_vm_runtime_error(vm, "sync.gather: list must contain only futures");
            return MS_NIL_VAL();
        }
        MsObjFuture* f = MS_AS_FUTURE(v);
        if (f->state == MS_FUTURE_RESOLVED) {
            gather_resolve(vm, ctx, i, f->result);
        } else if (f->state == MS_FUTURE_REJECTED) {
            gather_reject(vm, ctx, f->result);
            break;
        } else {
            ms_future_add_cb_waiter(vm, f, gather_resolve, gather_reject, ctx, i);
        }
        if (parent->state != MS_FUTURE_PENDING) break;
    }
    return MS_OBJ_VAL((MsObject*)parent);
}

/* ================================================================== */
/* Method dispatch for sync userdata objects                          */
/* Called from ms_builtin_invoke in vm_builtins.c                    */
/* ================================================================== */

bool ms_sync_invoke(MsVM* vm, MsObjUserdata* ud,
                    MsObjString* method, int argc, MsValue* argv,
                    MsValue* out) {
    const char* tag = ud->type_tag;
    const char* m   = method->data;

    /* ---- Once ---- */
    if (strcmp(tag, ONCE_TAG) == 0) {
        SyncOnce* o = (SyncOnce*)ud->data;
        if (strcmp(m, "do") == 0) {
            if (!o->done) {
                o->done = true;
                MsValue result;
                ms_vm_call_sync(vm, o->fn, NULL, 0, &result);
            }
            *out = MS_NIL_VAL();
            return true;
        }
        if (strcmp(m, "done") == 0) {
            *out = MS_BOOL_VAL(o->done);
            return true;
        }
        return false;
    }

    /* ---- WaitGroup ---- */
    if (strcmp(tag, WG_TAG) == 0) {
        SyncWG* w = (SyncWG*)ud->data;
        if (strcmp(m, "add") == 0) {
            int n = 0;
            if (argc >= 1 && MS_IS_INT(argv[0]))     n = (int)MS_AS_INT(argv[0]);
            else if (argc >= 1 && MS_IS_NUMBER(argv[0])) n = (int)MS_AS_NUMBER(argv[0]);
            w->count += n;
            *out = MS_NIL_VAL();
            return true;
        }
        if (strcmp(m, "done") == 0) {
            w->count--;
            if (w->count <= 0) {
                w->count = 0;
                for (int i = 0; i < w->waiters.count; i++) {
                    MsObjFuture* f = MS_AS_FUTURE(w->waiters.data[i]);
                    ms_future_resolve(vm, f, MS_NIL_VAL());
                }
                valarr_free(&w->waiters);
                valarr_init(&w->waiters);
            }
            *out = MS_NIL_VAL();
            return true;
        }
        if (strcmp(m, "wait") == 0) {
            MsObjFuture* f = ms_obj_future_new(vm);
            if (w->count <= 0)
                ms_future_resolve(vm, f, MS_NIL_VAL());
            else
                valarr_push(&w->waiters, MS_OBJ_VAL((MsObject*)f));
            *out = MS_OBJ_VAL((MsObject*)f);
            return true;
        }
        return false;
    }

    /* ---- Channel ---- */
    if (strcmp(tag, CH_TAG) == 0) {
        SyncChan* c = (SyncChan*)ud->data;

        if (strcmp(m, "close") == 0) {
            c->closed = true;
            for (int i = 0; i < c->recv_q.count; i++)
                ms_future_resolve(vm, MS_AS_FUTURE(c->recv_q.data[i]), MS_NIL_VAL());
            valarr_free(&c->recv_q);
            valarr_init(&c->recv_q);
            *out = MS_NIL_VAL();
            return true;
        }
        if (strcmp(m, "is_closed") == 0) {
            *out = MS_BOOL_VAL(c->closed);
            return true;
        }
        if (strcmp(m, "len") == 0) {
            *out = MS_INT_VAL((ms_i64)c->buf.count);
            return true;
        }
        if (strcmp(m, "send") == 0) {
            MsObjFuture* f   = ms_obj_future_new(vm);
            MsValue      val = (argc >= 1) ? argv[0] : MS_NIL_VAL();
            if (c->closed) {
                ms_future_reject(vm, f,
                    MS_OBJ_VAL(ms_obj_string_copy(vm, "send on closed channel", 22)));
                *out = MS_OBJ_VAL((MsObject*)f);
                return true;
            }
            /* Hand off to a parked receiver */
            if (c->recv_q.count > 0) {
                MsObjFuture* rf = MS_AS_FUTURE(valarr_shift(&c->recv_q));
                ms_future_resolve(vm, rf, val);
                ms_future_resolve(vm, f, MS_NIL_VAL());
                *out = MS_OBJ_VAL((MsObject*)f);
                return true;
            }
            /* Buffered with space */
            if (c->cap > 0 && c->buf.count < c->cap) {
                valarr_push(&c->buf, val);
                ms_future_resolve(vm, f, MS_NIL_VAL());
                *out = MS_OBJ_VAL((MsObject*)f);
                return true;
            }
            /* Park sender */
            valarr_push(&c->send_q, MS_OBJ_VAL((MsObject*)f));
            valarr_push(&c->send_q, val);
            *out = MS_OBJ_VAL((MsObject*)f);
            return true;
        }
        if (strcmp(m, "recv") == 0) {
            MsObjFuture* f = ms_obj_future_new(vm);
            /* Item in buffer */
            if (c->buf.count > 0) {
                MsValue val = valarr_shift(&c->buf);
                if (c->send_q.count >= 2) {
                    MsObjFuture* sf = MS_AS_FUTURE(valarr_shift(&c->send_q));
                    MsValue      sv = valarr_shift(&c->send_q);
                    valarr_push(&c->buf, sv);
                    ms_future_resolve(vm, sf, MS_NIL_VAL());
                }
                ms_future_resolve(vm, f, val);
                *out = MS_OBJ_VAL((MsObject*)f);
                return true;
            }
            /* Parked sender */
            if (c->send_q.count >= 2) {
                MsObjFuture* sf = MS_AS_FUTURE(valarr_shift(&c->send_q));
                MsValue      sv = valarr_shift(&c->send_q);
                ms_future_resolve(vm, sf, MS_NIL_VAL());
                ms_future_resolve(vm, f, sv);
                *out = MS_OBJ_VAL((MsObject*)f);
                return true;
            }
            /* Closed or park receiver */
            if (c->closed)
                ms_future_resolve(vm, f, MS_NIL_VAL());
            else
                valarr_push(&c->recv_q, MS_OBJ_VAL((MsObject*)f));
            *out = MS_OBJ_VAL((MsObject*)f);
            return true;
        }
        return false;
    }

    return false;
}

/* ================================================================== */
/* Module init                                                         */
/* ================================================================== */

static const MsNativeDef sync_natives[] = {
    {"new_once",       sync_new_once,       1},
    {"new_wait_group", sync_new_wait_group, 0},
    {"new_channel",    sync_new_channel,    -1},
    {"gather",         sync_gather,         1},
    {"race",           sync_race,           1},
    {NULL, NULL, 0}
};

void ms_module_sync_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, sync_natives);
}
