#include "ms/memory.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/vtable.h"
#include "ms/value.h"
#include "ms/chunk.h"
#include "ms/consts.h"
#include <stdlib.h>

/* compiler_impl.h is a src-private header */
#include "compiler_impl.h"

/* ---- mark helpers ---- */

void ms_mark_object(MsVM* vm, MsObject* obj) {
    if (obj == NULL || obj->is_marked) return;
    obj->is_marked = true;
    if (vm->gray_count >= vm->gray_capacity) {
        vm->gray_capacity = vm->gray_capacity < 8 ? 8 : vm->gray_capacity * 2;
        vm->gray_stack = (MsObject**)realloc(vm->gray_stack,
                         sizeof(MsObject*) * (size_t)vm->gray_capacity);
        if (!vm->gray_stack) abort();
    }
    vm->gray_stack[vm->gray_count++] = obj;
}

void ms_mark_value(MsVM* vm, MsValue val) {
    if (MS_IS_OBJECT(val))
        ms_mark_object(vm, MS_AS_OBJECT(val));
}

void ms_mark_table(MsVM* vm, MsTable* table) {
    for (int i = 0; i < table->capacity; i++) {
        MsEntry* e = &table->entries[i];
        if (e->key == NULL || e->key == MS_TABLE_TOMBSTONE) continue;
        ms_mark_object(vm, (MsObject*)e->key);
        ms_mark_value(vm, e->value);
    }
}

/* ---- trace chunk constants ---- */

static void mark_chunk_constants(MsVM* vm, MsChunk* chunk) {
    for (int i = 0; i < chunk->constants.count; i++)
        ms_mark_value(vm, chunk->constants.data[i]);
}

/* ---- blacken (trace children of a gray object) ---- */

static void blacken_object(MsVM* vm, MsObject* obj) {
    switch (obj->type) {
    case MS_OBJ_STRING:
        break;
    case MS_OBJ_FUNCTION: {
        MsObjFunction* fn = (MsObjFunction*)obj;
        ms_mark_object(vm, (MsObject*)fn->name);
        mark_chunk_constants(vm, &fn->chunk);
        break;
    }
    case MS_OBJ_NATIVE: {
        MsObjNative* n = (MsObjNative*)obj;
        ms_mark_object(vm, (MsObject*)n->name);
        break;
    }
    case MS_OBJ_CLOSURE: {
        MsObjClosure* cl = (MsObjClosure*)obj;
        ms_mark_object(vm, (MsObject*)cl->function);
        for (int i = 0; i < cl->upvalue_count; i++)
            ms_mark_object(vm, (MsObject*)cl->upvalues[i]);
        break;
    }
    case MS_OBJ_UPVALUE: {
        MsObjUpvalue* uv = (MsObjUpvalue*)obj;
        ms_mark_value(vm, uv->closed);
        break;
    }
    case MS_OBJ_CLASS: {
        MsObjClass* klass = (MsObjClass*)obj;
        ms_mark_object(vm, (MsObject*)klass->name);
        ms_mark_table(vm, &klass->methods);
        if (klass->superclass) ms_mark_object(vm, (MsObject*)klass->superclass);
        break;
    }
    case MS_OBJ_INSTANCE: {
        MsObjInstance* inst = (MsObjInstance*)obj;
        ms_mark_object(vm, (MsObject*)inst->klass);
        /* Mark all inline and overflow fields */
        int fc = inst->field_count;
        int inline_c = fc < MS_SBO_FIELDS ? fc : MS_SBO_FIELDS;
        for (int i = 0; i < inline_c; i++)
            ms_mark_value(vm, inst->inline_fields[i]);
        if (inst->overflow_fields) {
            int ov_c = fc - MS_SBO_FIELDS;
            for (int i = 0; i < ov_c; i++)
                ms_mark_value(vm, inst->overflow_fields[i]);
        }
        break;
    }
    case MS_OBJ_BOUND_METHOD: {
        MsObjBoundMethod* bm = (MsObjBoundMethod*)obj;
        ms_mark_value(vm, bm->receiver);
        ms_mark_object(vm, (MsObject*)bm->method);
        break;
    }
    case MS_OBJ_LIST: {
        MsObjList* list = (MsObjList*)obj;
        for (int i = 0; i < list->items.count; i++)
            ms_mark_value(vm, list->items.data[i]);
        break;
    }
    case MS_OBJ_MAP: {
        MsObjMap* map = (MsObjMap*)obj;
        for (int i = 0; i < map->table.capacity; i++) {
            MsVEntry* e = &map->table.entries[i];
            if (!e->used) continue;
            ms_mark_value(vm, e->key);
            ms_mark_value(vm, e->value);
        }
        break;
    }
    case MS_OBJ_TUPLE: {
        MsObjTuple* tup = (MsObjTuple*)obj;
        for (int i = 0; i < tup->count; i++)
            ms_mark_value(vm, tup->items[i]);
        break;
    }
    case MS_OBJ_COROUTINE: {
        MsObjCoroutine* co = (MsObjCoroutine*)obj;
        ms_mark_object(vm, (MsObject*)co->closure);
        ms_mark_value(vm, co->yield_value);
        for (MsValue* v = co->ctx.stack; v < co->ctx.stack_top; v++)
            ms_mark_value(vm, *v);
        for (int i = 0; i < co->ctx.frame_count; i++)
            ms_mark_object(vm, (MsObject*)co->ctx.frames[i].closure);
        for (MsObjUpvalue* uv = co->ctx.open_upvalues; uv; uv = uv->next)
            ms_mark_object(vm, (MsObject*)uv);
        break;
    }
    case MS_OBJ_FUTURE: {
        MsObjFuture* f = (MsObjFuture*)obj;
        if (f->coro) ms_mark_object(vm, (MsObject*)f->coro);
        ms_mark_value(vm, f->result);
        for (MsWaiter* w = f->waiters; w; w = w->next)
            if (w->type == MS_WAITER_CORO)
                ms_mark_object(vm, (MsObject*)w->u.coro.coro);
        break;
    }
    case MS_OBJ_MODULE: {
        MsObjModule* mod = (MsObjModule*)obj;
        ms_mark_object(vm, (MsObject*)mod->name);
        ms_mark_object(vm, (MsObject*)mod->path);
        ms_mark_table(vm, &mod->exports);
        break;
    }
    default:
        break;
    }
}

/* ---- mark compiler roots ---- */

static void mark_compiler_roots(MsVM* vm) {
    MsCompiler* c = (MsCompiler*)vm->compiler;
    while (c) {
        ms_mark_object(vm, (MsObject*)c->function);
        c = (MsCompiler*)c->enclosing;
    }
}

/* ---- Phase 1: mark roots ---- */

static void mark_roots(MsVM* vm) {
    MsExecCtx* ctx = vm->ctx;
    for (MsValue* slot = ctx->stack; slot < ctx->stack_top; slot++)
        ms_mark_value(vm, *slot);
    for (int i = 0; i < ctx->frame_count; i++)
        ms_mark_object(vm, (MsObject*)ctx->frames[i].closure);
    for (MsObjUpvalue* uv = ctx->open_upvalues; uv; uv = uv->next)
        ms_mark_object(vm, (MsObject*)uv);
    ms_mark_table(vm, &vm->globals);
    ms_mark_table(vm, &vm->module_cache);
    if (vm->compiler) mark_compiler_roots(vm);
    if (vm->init_string) ms_mark_object(vm, (MsObject*)vm->init_string);
    for (int i = 0; i < 128; i++) {
        if (vm->ascii_cache[i]) ms_mark_object(vm, (MsObject*)vm->ascii_cache[i]);
    }
}

/* ---- Phase 2: trace gray stack ---- */

static void trace_references(MsVM* vm) {
    while (vm->gray_count > 0) {
        MsObject* obj = vm->gray_stack[--vm->gray_count];
        blacken_object(vm, obj);
    }
}

/* ---- Minor GC sweep: young generation only ---- */

static void sweep_young(MsVM* vm) {
    MsObject** obj = &vm->young_objects;
    while (*obj) {
        MsObject* cur = *obj;
        if (!cur->is_marked) {
            *obj = cur->next;
            ms_object_free(vm, cur);
        } else {
            cur->is_marked = false;
            cur->age++;
            if (cur->age >= MS_GC_PROMOTE_AGE) {
                /* promote to old generation */
                *obj = cur->next;
                cur->generation = 1;
                cur->next = vm->old_objects;
                vm->old_objects = cur;
            } else {
                obj = &cur->next;
            }
        }
    }
}

/* ---- Phase 3: sweep one object list ---- */

static void sweep_list(MsVM* vm, MsObject** head) {
    while (*head) {
        if ((*head)->is_marked) {
            (*head)->is_marked = false;
            head = &(*head)->next;
        } else {
            MsObject* dead = *head;
            *head = dead->next;
            ms_object_free(vm, dead);
        }
    }
}

static void sweep_all(MsVM* vm) {
    sweep_list(vm, &vm->young_objects);
    sweep_list(vm, &vm->old_objects);
    sweep_list(vm, &vm->objects); /* legacy list from pre-generational allocs */
}

/* ---- Incremental GC step ---- */

void ms_gc_incremental_step(MsVM* vm) {
#ifdef MSLANG_VM_STATS
    vm->stats.incremental_step_count++;
    if (vm->bytes_allocated > vm->stats.bytes_allocated_peak)
        vm->stats.bytes_allocated_peak = vm->bytes_allocated;
#endif
    switch (vm->gc_phase) {
    case MS_GC_IDLE:
        mark_roots(vm);
        vm->gc_phase = MS_GC_MARKING;
        break;
    case MS_GC_MARKING:
        for (int i = 0; i < MS_GC_INCR_WORK && vm->gray_count > 0; i++) {
            MsObject* obj = vm->gray_stack[--vm->gray_count];
            blacken_object(vm, obj);
        }
        if (vm->gray_count == 0) {
            ms_table_remove_white(&vm->strings);
            vm->sweep_prev  = &vm->objects;
            vm->sweep_cursor = vm->objects;
            vm->gc_phase    = MS_GC_SWEEPING;
        }
        break;
    case MS_GC_SWEEPING: {
        int steps = 0;
        while (vm->sweep_cursor && steps < MS_GC_INCR_WORK) {
            MsObject* obj = vm->sweep_cursor;
            if (!obj->is_marked) {
                *vm->sweep_prev  = obj->next;
                vm->sweep_cursor = obj->next;
                ms_object_free(vm, obj);
            } else {
                obj->is_marked   = false;
                vm->sweep_prev   = &obj->next;
                vm->sweep_cursor = obj->next;
            }
            steps++;
        }
        if (!vm->sweep_cursor) {
            vm->gc_phase = MS_GC_IDLE;
            vm->next_gc  = vm->bytes_allocated < 512 * 1024
                           ? 1024 * 1024
                           : vm->bytes_allocated * 2;
        }
        break;
    }
    }
}

/* ---- Minor GC ---- */

void ms_gc_collect_minor(MsVM* vm) {
    mark_roots(vm);
    /* mark all objects in remembered_set */
    for (int i = 0; i < vm->remembered_count; i++)
        ms_mark_object(vm, vm->remembered_set[i]);
    trace_references(vm);
    ms_table_remove_white(&vm->strings);
    sweep_young(vm);
    /* clear remembered set */
    for (int i = 0; i < vm->remembered_count; i++)
        vm->remembered_set[i]->in_remembered_set = false;
    vm->remembered_count = 0;
    vm->young_bytes = 0;
    vm->minor_count++;
#ifdef MSLANG_VM_STATS
    vm->stats.minor_gc_count++;
#endif
}

/* ---- Major GC ---- */

void ms_gc_collect(MsVM* vm) {
    mark_roots(vm);
    /* also mark remembered set roots */
    for (int i = 0; i < vm->remembered_count; i++)
        ms_mark_object(vm, vm->remembered_set[i]);
    trace_references(vm);
    ms_table_remove_white(&vm->strings);
    sweep_all(vm);
    /* clear remembered set */
    for (int i = 0; i < vm->remembered_count; i++)
        vm->remembered_set[i]->in_remembered_set = false;
    vm->remembered_count = 0;
    vm->young_bytes = 0;
    vm->minor_count = 0;
    vm->next_gc = vm->bytes_allocated < 512 * 1024
                  ? 1024 * 1024
                  : vm->bytes_allocated * 2;
    /* reset incremental state */
    vm->gc_phase     = MS_GC_IDLE;
    vm->sweep_cursor = NULL;
    vm->sweep_prev   = NULL;
#ifdef MSLANG_VM_STATS
    vm->stats.major_gc_count++;
    /* Count live objects after major GC */
    {
        int live = 0;
        MsObject* o = vm->objects;
        while (o) { live++; o = o->next; }
        o = vm->old_objects;
        while (o) { live++; o = o->next; }
        o = vm->young_objects;
        while (o) { live++; o = o->next; }
        vm->stats.live_objects_after_final_gc = live;
    }
#endif
}
