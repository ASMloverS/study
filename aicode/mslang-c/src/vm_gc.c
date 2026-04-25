#include "ms/memory.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/value.h"
#include "ms/chunk.h"
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
        /* no children */
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
    default:
        break;
    }
}

/* ---- mark compiler roots (GC-safe during compilation) ---- */

static void mark_compiler_roots(MsVM* vm) {
    MsCompiler* c = (MsCompiler*)vm->compiler;
    while (c) {
        ms_mark_object(vm, (MsObject*)c->function);
        c = (MsCompiler*)c->enclosing;
    }
}

/* ---- Phase 1: mark roots ---- */

static void mark_roots(MsVM* vm) {
    for (MsValue* slot = vm->stack; slot < vm->stack_top; slot++)
        ms_mark_value(vm, *slot);
    for (int i = 0; i < vm->frame_count; i++)
        ms_mark_object(vm, (MsObject*)vm->frames[i].closure);
    for (MsObjUpvalue* uv = vm->open_upvalues; uv; uv = uv->next)
        ms_mark_object(vm, (MsObject*)uv);
    ms_mark_table(vm, &vm->globals);
    if (vm->compiler) mark_compiler_roots(vm);
    if (vm->init_string) ms_mark_object(vm, (MsObject*)vm->init_string);
}

/* ---- Phase 2: trace gray stack ---- */

static void trace_references(MsVM* vm) {
    while (vm->gray_count > 0) {
        MsObject* obj = vm->gray_stack[--vm->gray_count];
        blacken_object(vm, obj);
    }
}

/* ---- Phase 3: sweep ---- */

static void sweep(MsVM* vm) {
    MsObject** obj = &vm->objects;
    while (*obj) {
        if ((*obj)->is_marked) {
            (*obj)->is_marked = false;
            obj = &(*obj)->next;
        } else {
            MsObject* dead = *obj;
            *obj = dead->next;
            ms_object_free(vm, dead);
        }
    }
}

/* ---- collect ---- */

void ms_gc_collect(MsVM* vm) {
    mark_roots(vm);
    trace_references(vm);
    /* remove white (unmarked) interned strings before sweep */
    ms_table_remove_white(&vm->strings);
    sweep(vm);
    vm->next_gc = vm->bytes_allocated < 512 * 1024
                  ? 1024 * 1024
                  : vm->bytes_allocated * 2;
}
