#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/module.h"
#include "ms/opcode.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/vtable.h"
#include "ms/value.h"
#include "ms/shape.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Instance field helpers (Shape + SBO) ---- */

static MsValue* inst_field_ptr(MsObjInstance* inst, int slot) {
    return ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot);
}

/* Get a field value by slot index. */
static MsValue inst_get_by_slot(MsObjInstance* inst, int slot) {
    return *inst_field_ptr(inst, slot);
}

/* Set a field value by slot; grows overflow array if needed. */
static void inst_set_by_slot(struct MsVM* vm, MsObjInstance* inst,
                              int slot, MsValue val) {
    if (slot >= MS_SBO_FIELDS) {
        int ov_idx = slot - MS_SBO_FIELDS;
        int cur_ov = (inst->field_count > MS_SBO_FIELDS)
                     ? inst->field_count - MS_SBO_FIELDS : 0;
        if (ov_idx >= cur_ov) {
            /* Grow overflow array */
            int new_ov = ov_idx + 1;
            inst->overflow_fields = (MsValue*)ms_reallocate(
                vm, inst->overflow_fields,
                sizeof(MsValue) * (size_t)cur_ov,
                sizeof(MsValue) * (size_t)new_ov);
            for (int i = cur_ov; i < new_ov; i++)
                inst->overflow_fields[i] = MS_NIL_VAL();
        }
    }
    *inst_field_ptr(inst, slot) = val;
    if (slot >= inst->field_count)
        inst->field_count = slot + 1;
}

/* ---- IC helpers ---- */

/* Ensure fn->ic is allocated and large enough for idx (handles stale ic_count after deserialization). */
static MsInlineCache* ensure_ic(MsVM* vm, MsObjFunction* fn, int idx) {
    int needed = idx + 1;
    if (!fn->ic || fn->ic_count < needed) {
        size_t old_sz = (size_t)fn->ic_count * sizeof(MsInlineCache);
        fn->ic = (MsInlineCache*)ms_reallocate(vm, fn->ic, old_sz,
                     (size_t)needed * sizeof(MsInlineCache));
        for (int i = fn->ic_count; i < needed; i++) {
            fn->ic[i].count       = 0;
            fn->ic[i].megamorphic = false;
        }
        fn->ic_count = needed;
    }
    return &fn->ic[idx];
}

/* Try IC hit for GETPROP; returns slot index or -1 on miss. */
static int ic_get_field_slot(MsInlineCache* ic, MsObjInstance* inst) {
    if (ic->megamorphic) return -1;
    for (int i = 0; i < ic->count; i++) {
        if (ic->entries[i].kind == MS_IC_FIELD &&
            ic->entries[i].shape_id == inst->shape->id) {
            return (int)ic->entries[i].slot_index;
        }
    }
    return -1;
}

/* Update IC with a new field hit. */
static void ic_update_field(MsInlineCache* ic, MsObjInstance* inst, int slot) {
    if (ic->megamorphic) return;
    /* Check if shape_id already recorded */
    for (int i = 0; i < ic->count; i++) {
        if (ic->entries[i].shape_id == inst->shape->id) return;
    }
    if (ic->count >= MS_IC_PIC_SIZE) {
        ic->megamorphic = true;
        return;
    }
    MsICEntry* e = &ic->entries[ic->count++];
    e->shape_id   = inst->shape->id;
    e->slot_index = (uint32_t)slot;
    e->kind       = MS_IC_FIELD;
    e->cached     = MS_NIL_VAL();
}

/* ---- forward decls ---- */
static MsInterpretResult call_value(MsVM* vm, MsValue callee,
                                    int arg_count, int ret_dst);
static MsInterpretResult vm_run_inner(MsVM* vm);
void ms_vm_register_natives(MsVM* vm);

/* ---- upvalue helpers ---- */

static MsObjUpvalue* capture_upvalue(MsVM* vm, MsValue* local) {
    MsObjUpvalue* prev = NULL;
    MsObjUpvalue* uv   = vm->ctx->open_upvalues;
    while (uv != NULL && uv->location > local) {
        prev = uv;
        uv   = uv->next;
    }
    if (uv != NULL && uv->location == local) return uv;
    MsObjUpvalue* created = ms_obj_upvalue_new(vm, local);
    created->next = uv;
    if (prev) prev->next = created;
    else      vm->ctx->open_upvalues = created;
    return created;
}

static void close_upvalues(MsVM* vm, MsValue* last) {
    if (vm->ctx->open_upvalues == NULL) return;
    while (vm->ctx->open_upvalues->location >= last) {
        MsObjUpvalue* uv = vm->ctx->open_upvalues;
        uv->closed   = *uv->location;
        uv->location = &uv->closed;
        vm->ctx->open_upvalues = uv->next;
        if (vm->ctx->open_upvalues == NULL) break;
    }
}

/* ---- init / free ---- */

void ms_vm_init(MsVM* vm) {
    /* Wire host execution context to the static buffers before any use of vm->ctx */
    vm->host_ctx.stack          = vm->host_stack;
    vm->host_ctx.stack_top      = vm->host_stack;
    vm->host_ctx.frames         = vm->host_frames;
    vm->host_ctx.frame_count    = 0;
    vm->host_ctx.frame_capacity = MS_FRAMES_MAX;
    vm->host_ctx.stack_capacity = MS_STACK_SIZE;
    vm->host_ctx.open_upvalues  = NULL;
    vm->ctx = &vm->host_ctx;

    memset(vm->host_stack,  0, sizeof(vm->host_stack));
    memset(vm->host_frames, 0, sizeof(vm->host_frames));
    vm->exception_count      = 0;
    vm->objects              = NULL;
    vm->young_objects        = NULL;
    vm->old_objects          = NULL;
    vm->remembered_set       = NULL;
    vm->remembered_count     = 0;
    vm->remembered_capacity  = 0;
    vm->young_bytes          = 0;
    vm->bytes_allocated      = 0;
    vm->next_gc              = 1024 * 1024;
    vm->minor_count          = 0;
    vm->init_string          = NULL;
    vm->compiler             = NULL;
    vm->gray_stack           = NULL;
    vm->gray_count           = 0;
    vm->gray_capacity        = 0;
    vm->current_coroutine = NULL;
    vm->gc_phase     = MS_GC_IDLE;
    vm->sweep_cursor = NULL;
    vm->sweep_prev   = NULL;
    ms_pool_init(&vm->upvalue_pool, sizeof(MsObjUpvalue));
    ms_pool_init(&vm->bound_pool,   sizeof(MsObjBoundMethod));
    ms_table_init(&vm->globals);
    ms_table_init(&vm->strings);
    ms_table_init(&vm->module_cache);
    vm->init_string = ms_obj_string_copy(vm, "init", 4);
    memset(vm->ascii_cache, 0, sizeof(vm->ascii_cache));
    for (int i = 0; i < 128; i++) {
        char c = (char)i;
        vm->ascii_cache[i] = ms_obj_string_copy(vm, &c, 1);
    }
    vm->loop_inited = false;
    ms_vm_register_natives(vm);
#ifdef MSLANG_VM_STATS
    memset(&vm->stats, 0, sizeof(vm->stats));
#endif
}

void ms_vm_free(MsVM* vm) {
    for (int i = 0; i < MS_FRAMES_MAX; i++) {
        if (vm->host_frames[i].deferred) {
            free(vm->host_frames[i].deferred);
            vm->host_frames[i].deferred = NULL;
        }
    }
    ms_table_free(&vm->globals);
    ms_table_free(&vm->strings);
    ms_table_free(&vm->module_cache);
    free(vm->gray_stack);
    vm->gray_stack    = NULL;
    vm->gray_count    = 0;
    vm->gray_capacity = 0;
    free(vm->remembered_set);
    vm->remembered_set      = NULL;
    vm->remembered_count    = 0;
    vm->remembered_capacity = 0;
    /* Free all objects across all generations */
    MsObject* obj = vm->young_objects;
    while (obj) { MsObject* n = obj->next; ms_object_free(vm, obj); obj = n; }
    vm->young_objects = NULL;
    obj = vm->old_objects;
    while (obj) { MsObject* n = obj->next; ms_object_free(vm, obj); obj = n; }
    vm->old_objects = NULL;
    obj = vm->objects;
    while (obj) { MsObject* n = obj->next; ms_object_free(vm, obj); obj = n; }
    vm->objects = NULL;
    ms_pool_destroy(&vm->upvalue_pool);
    ms_pool_destroy(&vm->bound_pool);
    if (vm->loop_inited) {
        ms_loop_destroy(&vm->event_loop);
        vm->loop_inited = false;
    }
}

/* ---- stats API ---- */

#ifdef MSLANG_VM_STATS
void ms_vm_get_stats(const MsVM* vm, MsVMStats* out) {
    *out = vm->stats;
}
void ms_vm_reset_stats(MsVM* vm) {
    memset(&vm->stats, 0, sizeof(vm->stats));
}
#endif

/* ---- runtime error ---- */

void ms_vm_runtime_error(MsVM* vm, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "RuntimeError: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    for (int i = vm->ctx->frame_count - 1; i >= 0; i--) {
        MsCallFrame* frame = &vm->ctx->frames[i];
        MsObjFunction* fn = frame->closure->function;
        ptrdiff_t offset = frame->ip - fn->chunk.code - 1;
        int line = ms_chunk_get_line(&fn->chunk, (int)offset);
        const char* name = fn->name ? fn->name->data : "<script>";
        fprintf(stderr, "  [line %d] in %s\n", line, name);
    }
    vm->ctx->frame_count = 0;
    vm->ctx->stack_top   = vm->ctx->stack;
}

/* ---- native define ---- */

void ms_vm_define_native(MsVM* vm, const char* name, MsNativeFn fn, int arity) {
    MsObjNative* nat = ms_obj_native_new(vm, fn, name, arity);
    MsObjString* key = ms_obj_string_copy(vm, name, (int)strlen(name));
    ms_table_set(&vm->globals, key, MS_OBJ_VAL(nat));
}

/* ---- ms_vm_interpret ---- */

MsInterpretResult ms_vm_interpret(MsVM* vm, const char* source, const char* path) {
    MsDiagnostic diags[32];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, source, path, diags, &diag_count, 32);
    if (!fn) {
        for (int i = 0; i < diag_count; i++) {
            fprintf(stderr, "[line %d] Error: %s\n",
                    diags[i].line, diags[i].message);
        }
        return MS_INTERPRET_COMPILE_ERROR;
    }
    if (path)
        fn->script_path = ms_obj_string_copy(vm, path, (int)strlen(path));

    MsObjClosure* cl = ms_obj_closure_new(vm, fn);
    MsCallFrame*  frame = &vm->ctx->frames[0];
    frame->closure = cl;
    frame->ip      = fn->chunk.code;
    frame->slots   = vm->ctx->stack;
    vm->ctx->frame_count = 1;
    int need = fn->max_stack_size + 1;
    if (need < 1) need = 1;
    vm->ctx->stack_top = vm->ctx->stack + need;
    return vm_run_inner(vm);
}

MsInterpretResult ms_vm_run(MsVM* vm) {
    return vm_run_inner(vm);
}

MsInterpretResult ms_vm_coro_resume(MsVM* vm, MsObjCoroutine* co,
                                     MsValue sent, MsValue* out) {
    if (co->state == MS_CORO_DEAD || co->state == MS_CORO_RUNNING)
        return MS_INTERPRET_RUNTIME_ERROR;

    bool first_resume = (co->state == MS_CORO_CREATED);

    /* O(1) context swap: save host ctx pointer, switch to coroutine ctx */
    MsExecCtx*      saved_ctx  = vm->ctx;
    int             saved_exc  = vm->exception_count;
    MsObjCoroutine* saved_coro = vm->current_coroutine;

    vm->ctx               = (MsExecCtx*)&co->ctx;  /* MsCoroCtx layout matches MsExecCtx */
    vm->exception_count   = 0;
    vm->current_coroutine = co;
    co->state             = MS_CORO_RUNNING;

    if (!first_resume) {
        /* Inject sent value as the yield expression's result */
        MsCallFrame* co_frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
        MsInstruction prev = *(co_frame->ip - 1);
        int yield_reg = MS_GET_A(prev);
        co_frame->slots[yield_reg] = sent;
    }

    MsInterpretResult cr = vm_run_inner(vm);

    /* Restore host context (O(1)) */
    vm->ctx               = saved_ctx;
    vm->exception_count   = saved_exc;
    vm->current_coroutine = saved_coro;

    *out = MS_NIL_VAL();
    if (cr == MS_INTERPRET_YIELD) {
        *out = co->yield_value;
    } else if (cr == MS_INTERPRET_OK) {
        *out = vm->call_result;
        co->state = MS_CORO_DEAD;
    } else {
        co->state = MS_CORO_DEAD;
    }

    return (cr == MS_INTERPRET_YIELD || cr == MS_INTERPRET_OK)
           ? MS_INTERPRET_OK : MS_INTERPRET_RUNTIME_ERROR;
}

/* ms_vm_call_sync: call a closure from C, synchronously.
   Saves all VM frame state, runs closure in isolation, restores state.
   RETURN stores result in vm->call_result; we read it back. */
MsInterpretResult ms_vm_call_sync(MsVM* vm, MsValue callee,
                                   MsValue* argv, int argc, MsValue* out) {
    if (!MS_IS_CLOSURE(callee) && !MS_IS_NATIVE(callee)) {
        ms_vm_runtime_error(vm, "Callback is not a function.");
        return MS_INTERPRET_RUNTIME_ERROR;
    }

    if (MS_IS_NATIVE(callee)) {
        MsObjNative* nat = MS_AS_NATIVE(callee);
        *out = nat->function(vm, argc, argv);
        return MS_INTERPRET_OK;
    }

    MsObjClosure* cl = MS_AS_CLOSURE(callee);
    MsObjFunction* fn = cl->function;

    /* Save active execution context pointer; switch to host ctx for the call. */
    MsExecCtx* saved_ctx = vm->ctx;
    vm->ctx = &vm->host_ctx;

    /* Save host state */
    int saved_fc = vm->ctx->frame_count;
    MsCallFrame saved_frames[MS_FRAMES_MAX];
    for (int i = 0; i < saved_fc; i++) saved_frames[i] = vm->ctx->frames[i];
    MsValue*      saved_top = vm->ctx->stack_top;
    MsObjUpvalue* saved_uv  = vm->ctx->open_upvalues;

    /* Place args in upper half of host stack (isolated from active execution). */
    MsValue* base = vm->host_stack + MS_STACK_SIZE / 2;
    for (int i = 0; i < argc; i++) base[i] = argv[i];

    /* Run closure as sole frame; RETURN stores result in vm->call_result. */
    vm->ctx->frame_count       = 1;
    vm->ctx->open_upvalues     = NULL;
    vm->ctx->frames[0].closure = cl;
    vm->ctx->frames[0].ip      = fn->chunk.code;
    vm->ctx->frames[0].slots   = base;
    vm->ctx->stack_top         = base + fn->max_stack_size + 1;

    vm->call_result = MS_NIL_VAL();
    MsInterpretResult r = vm_run_inner(vm);
    if (r == MS_INTERPRET_OK) *out = vm->call_result;

    /* Restore host state and active context */
    vm->ctx->frame_count   = saved_fc;
    vm->ctx->stack_top     = saved_top;
    vm->ctx->open_upvalues = saved_uv;
    for (int i = 0; i < saved_fc; i++) vm->ctx->frames[i] = saved_frames[i];
    vm->ctx = saved_ctx;

    return r;
}

/* ---- arithmetic helpers ---- */

static bool numeric_binop(MsValue b, MsValue c, int op, MsValue* out) {
    if (MS_IS_INT(b) && MS_IS_INT(c)) {
        ms_i64 bi = MS_AS_INT(b), ci = MS_AS_INT(c);
        switch (op) {
        case MS_OP_ADD: *out = MS_INT_VAL(bi + ci); return true;
        case MS_OP_SUB: *out = MS_INT_VAL(bi - ci); return true;
        case MS_OP_MUL: *out = MS_INT_VAL(bi * ci); return true;
        case MS_OP_DIV: *out = MS_NUMBER_VAL((double)bi / (double)ci); return true;
        case MS_OP_MOD:
            if (ci == 0) return false;
            *out = MS_INT_VAL(bi % ci);
            return true;
        default: return false;
        }
    }
    if (MS_IS_NUMERIC(b) && MS_IS_NUMERIC(c)) {
        double bd = ms_as_double(b), cd = ms_as_double(c);
        switch (op) {
        case MS_OP_ADD: *out = MS_NUMBER_VAL(bd + cd); return true;
        case MS_OP_SUB: *out = MS_NUMBER_VAL(bd - cd); return true;
        case MS_OP_MUL: *out = MS_NUMBER_VAL(bd * cd); return true;
        case MS_OP_DIV: *out = MS_NUMBER_VAL(bd / cd);  return true;
        case MS_OP_MOD: *out = MS_NUMBER_VAL(bd - (int)(bd / cd) * cd); return true;
        default: return false;
        }
    }
    return false;
}

static bool cmp_values(MsValue b, MsValue c, int op, bool* result) {
    if (MS_IS_INT(b) && MS_IS_INT(c)) {
        ms_i64 bi = MS_AS_INT(b), ci = MS_AS_INT(c);
        switch (op) {
        case MS_OP_LT: *result = bi < ci;  return true;
        case MS_OP_LE: *result = bi <= ci; return true;
        case MS_OP_EQ: *result = bi == ci; return true;
        default: return false;
        }
    }
    if (MS_IS_NUMERIC(b) && MS_IS_NUMERIC(c)) {
        double bd = ms_as_double(b), cd = ms_as_double(c);
        switch (op) {
        case MS_OP_LT: *result = bd < cd;  return true;
        case MS_OP_LE: *result = bd <= cd; return true;
        case MS_OP_EQ: *result = bd == cd; return true;
        default: return false;
        }
    }
    if (op == MS_OP_EQ) { *result = ms_value_equals(b, c); return true; }
    return false;
}

/* ---- quickening helpers ---- */

/* Lazily allocate the deopt counter array for fn, sized to code_count. */
static ms_u8* ensure_deopt(MsObjFunction* fn) {
    int need = fn->chunk.code_count;
    if (need <= 0) need = 1;
    if (fn->arith_deopt_size < need) {
        fn->arith_deopt = (ms_u8*)realloc(fn->arith_deopt, (size_t)need);
        if (fn->arith_deopt) {
            for (int i = fn->arith_deopt_size; i < need; i++)
                fn->arith_deopt[i] = 0;
        }
        fn->arith_deopt_size = need;
    }
    return fn->arith_deopt;
}

/* Increment deopt counter for instr at `offset`.
   Returns new counter value, or 255 if alloc failed. */
/* bump_deopt is called from DEOPT_AND_RESPECIALIZE; the vm pointer is not
   available here, so deopt_event_count is incremented at the call sites. */
static int bump_deopt(MsObjFunction* fn, int offset) {
    ms_u8* arr = ensure_deopt(fn);
    if (!arr) return 255;
    if (offset < 0 || offset >= fn->arith_deopt_size) return 255;
    if (arr[offset] < 255) arr[offset]++;
    return (int)arr[offset];
}

/* ---- stats tracking macros (used in call_value and dispatch loop) ---- */

#ifdef MSLANG_VM_STATS
#  define STATS_TRACK_FRAME(vm) \
    do { if ((vm)->ctx->frame_count > (vm)->stats.peak_frame_count) \
             (vm)->stats.peak_frame_count = (vm)->ctx->frame_count; } while (0)
#  define STATS_TRACK_DEOPT(vm) \
    do { (vm)->stats.deopt_event_count++; } while (0)
#else
#  define STATS_TRACK_FRAME(vm) ((void)0)
#  define STATS_TRACK_DEOPT(vm) ((void)0)
#endif

/* ---- call_value ---- */

static MsInterpretResult call_value(MsVM* vm, MsValue callee,
                                    int arg_count, int ret_dst) {
    if (MS_IS_CLOSURE(callee)) {
        MsObjClosure* cl  = MS_AS_CLOSURE(callee);
        MsObjFunction* fn = cl->function;
        if (fn->min_arity != -1 &&
            (arg_count < fn->min_arity || arg_count > fn->arity)) {
            ms_vm_runtime_error(vm, "Expected %d args but got %d.",
                                fn->arity, arg_count);
            return MS_INTERPRET_RUNTIME_ERROR;
        }
        /* Generator closure: create coroutine, copy args into its stack */
        if (fn->is_generator) {
            MsObjCoroutine* co = ms_obj_coroutine_new(vm, cl);
            /* Copy args into coroutine's stack buffer */
            MsCallFrame* caller = &vm->ctx->frames[vm->ctx->frame_count - 1];
            MsValue* caller_args = caller->slots + ret_dst + 1;
            for (int i = 0; i < arg_count; i++)
                co->stack_buf[i] = caller_args[i];
            /* Set up initial frame via ctx */
            int need = fn->max_stack_size + 1;
            co->ctx.stack_top = co->ctx.stack + need;
            MsCallFrame* f = &co->ctx.frames[0];
            f->closure        = cl;
            f->ip             = fn->chunk.code;
            f->slots          = co->ctx.stack;
            f->deferred       = NULL;
            f->deferred_count    = 0;
            f->deferred_capacity = 0;
            co->ctx.frame_count = 1;
            co->state = MS_CORO_CREATED;
            caller->slots[ret_dst] = MS_OBJ_VAL(co);
            return MS_INTERPRET_OK;
        }
        if (vm->ctx->frame_count >= MS_FRAMES_MAX) {
            ms_vm_runtime_error(vm, "Stack overflow.");
            return MS_INTERPRET_RUNTIME_ERROR;
        }
        MsCallFrame* new_frame = &vm->ctx->frames[vm->ctx->frame_count++];
        STATS_TRACK_FRAME(vm);
        new_frame->closure = cl;
        new_frame->ip      = fn->chunk.code;
        /* args are in slots ret_dst+1 .. ret_dst+arg_count of caller frame */
        new_frame->slots = vm->ctx->frames[vm->ctx->frame_count - 2].slots + ret_dst + 1;
        MsValue* new_top = new_frame->slots + fn->max_stack_size + 1;
        if (new_top > vm->ctx->stack_top) vm->ctx->stack_top = new_top;
        return MS_INTERPRET_OK;
    }
    if (MS_IS_NATIVE(callee)) {
        MsObjNative* nat = MS_AS_NATIVE(callee);
        int saved_fc = vm->ctx->frame_count;
        MsValue* argv = vm->ctx->frames[saved_fc - 1].slots + ret_dst + 1;
        MsValue result = nat->function(vm, arg_count, argv);
        if (vm->ctx->frame_count == 0) return MS_INTERPRET_RUNTIME_ERROR;
        vm->ctx->frames[vm->ctx->frame_count - 1].slots[ret_dst] = result;
        return MS_INTERPRET_OK;
    }
    if (MS_IS_CLASS(callee)) {
        MsObjClass* klass = MS_AS_CLASS(callee);
        MsObjInstance* inst = ms_obj_instance_new(vm, klass);
        /* store instance in ret_dst */
        vm->ctx->frames[vm->ctx->frame_count - 1].slots[ret_dst] = MS_OBJ_VAL(inst);
        /* call init if it exists */
        MsValue init_val;
        if (vm->init_string &&
            ms_table_get(&klass->methods, vm->init_string, &init_val)) {
            MsObjClosure* init_cl = MS_AS_CLOSURE(init_val);
            if (vm->ctx->frame_count >= MS_FRAMES_MAX) {
                ms_vm_runtime_error(vm, "Stack overflow.");
                return MS_INTERPRET_RUNTIME_ERROR;
            }
            MsCallFrame* new_frame = &vm->ctx->frames[vm->ctx->frame_count++];
            STATS_TRACK_FRAME(vm);
            new_frame->closure = init_cl;
            new_frame->ip      = init_cl->function->chunk.code;
            /* slot 0 = this (instance), then args */
            new_frame->slots = vm->ctx->frames[vm->ctx->frame_count - 2].slots + ret_dst;
            MsValue* new_top = new_frame->slots + init_cl->function->max_stack_size + 1;
            if (new_top > vm->ctx->stack_top) vm->ctx->stack_top = new_top;
        } else if (arg_count != 0) {
            ms_vm_runtime_error(vm, "Expected 0 arguments but got %d.", arg_count);
            return MS_INTERPRET_RUNTIME_ERROR;
        }
        return MS_INTERPRET_OK;
    }
    if (MS_IS_BOUND_METHOD(callee)) {
        MsObjBoundMethod* bm = MS_AS_BOUND_METHOD(callee);
        MsObjFunction* fn = bm->method->function;
        if (fn->min_arity != -1 &&
            (arg_count < fn->min_arity || arg_count > fn->arity)) {
            ms_vm_runtime_error(vm, "Expected %d args but got %d.",
                                fn->arity, arg_count);
            return MS_INTERPRET_RUNTIME_ERROR;
        }
        if (vm->ctx->frame_count >= MS_FRAMES_MAX) {
            ms_vm_runtime_error(vm, "Stack overflow.");
            return MS_INTERPRET_RUNTIME_ERROR;
        }
        MsCallFrame* new_frame = &vm->ctx->frames[vm->ctx->frame_count++];
        STATS_TRACK_FRAME(vm);
        new_frame->closure = bm->method;
        new_frame->ip      = fn->chunk.code;
        new_frame->slots   = vm->ctx->frames[vm->ctx->frame_count - 2].slots + ret_dst;
        /* slot 0 = receiver */
        new_frame->slots[0] = bm->receiver;
        MsValue* new_top = new_frame->slots + fn->max_stack_size + 1;
        if (new_top > vm->ctx->stack_top) vm->ctx->stack_top = new_top;
        return MS_INTERPRET_OK;
    }
    ms_vm_runtime_error(vm, "Can only call functions.");
    return MS_INTERPRET_RUNTIME_ERROR;
}

/* ---- defer / exception helpers ---- */

/* Execute deferred closures LIFO, then reset count.
   Each deferred closure is run as a fresh top-level invocation so that
   MS_OP_RETURN hits frame_count == 0 and exits cleanly. */
static void run_deferred(MsVM* vm, MsCallFrame* f) {
    for (int i = f->deferred_count - 1; i >= 0; i--) {
        MsObjClosure* cl = f->deferred[i];
        /* Save all execution state */
        int saved_fc  = vm->ctx->frame_count;
        int saved_exc = vm->exception_count;
        MsValue* saved_top = vm->ctx->stack_top;
        MsCallFrame saved_frame0 = vm->ctx->frames[0];

        /* Run deferred closure as sole frame (frame_count=1) */
        MsValue* base = vm->ctx->stack_top;
        MsValue* new_top = base + cl->function->max_stack_size + 1;
        MsValue* stack_end = vm->ctx->stack + vm->ctx->stack_capacity;
        if (new_top > stack_end)
            new_top = stack_end;
        vm->ctx->frame_count          = 1;
        vm->exception_count      = 0;
        vm->ctx->stack_top            = new_top;
        vm->ctx->frames[0].closure    = cl;
        vm->ctx->frames[0].ip         = cl->function->chunk.code;
        vm->ctx->frames[0].slots      = base;
        vm->ctx->frames[0].deferred   = NULL;
        vm->ctx->frames[0].deferred_count    = 0;
        vm->ctx->frames[0].deferred_capacity = 0;
        vm_run_inner(vm);

        /* Restore all execution state */
        vm->ctx->frame_count      = saved_fc;
        vm->exception_count  = saved_exc;
        vm->ctx->stack_top        = saved_top;
        vm->ctx->frames[0]        = saved_frame0;
    }
    if (f->deferred) {
        free(f->deferred);
        f->deferred = NULL;
    }
    f->deferred_count    = 0;
    f->deferred_capacity = 0;
}

/* Unwind to nearest handler; returns true if handler found. */
static bool throw_exception(MsVM* vm, MsValue error) {
    while (vm->exception_count > 0) {
        MsExceptionHandler* h = &vm->exception_handlers[vm->exception_count - 1];
        /* Unwind frames above the handler's frame */
        while (vm->ctx->frame_count - 1 > h->frame_index) {
            MsCallFrame* f = &vm->ctx->frames[vm->ctx->frame_count - 1];
            run_deferred(vm, f);
            close_upvalues(vm, f->slots);
            vm->ctx->frame_count--;
        }
        vm->exception_count--;
        MsCallFrame* target = &vm->ctx->frames[h->frame_index];
        target->ip                    = h->handler_ip;
        target->slots[h->catch_reg]   = error;
        vm->ctx->stack_top                 = h->stack_top;
        return true;
    }
    return false;
}

/* ---- module execution ---- */

MsInterpretResult ms_vm_execute_module(MsVM* vm, MsObjFunction* fn,
                                        MsObjModule* mod) {
    MsObjClosure* cl = ms_obj_closure_new(vm, fn);

    /* Always use host ctx for module execution (needs full host stack). */
    MsExecCtx* saved_ctx = vm->ctx;
    vm->ctx = &vm->host_ctx;

    /* Save host execution state */
    int saved_fc = vm->ctx->frame_count;
    MsCallFrame saved_frames[MS_FRAMES_MAX];
    for (int i = 0; i < saved_fc; i++) saved_frames[i] = vm->ctx->frames[i];
    MsValue*      saved_top    = vm->ctx->stack_top;
    MsObjUpvalue* saved_uv     = vm->ctx->open_upvalues;
    int           saved_exc    = vm->exception_count;
    MsTable       saved_globals = vm->globals;

    /* Give module its own globals table; pre-seed with native functions so
       module code can call print(), type(), etc. */
    ms_table_init(&vm->globals);
    for (int i = 0; i < saved_globals.capacity; i++) {
        MsEntry* e = &saved_globals.entries[i];
        if (e->key == NULL || e->key == MS_TABLE_TOMBSTONE) continue;
        if (MS_IS_NATIVE(e->value))
            ms_table_set(&vm->globals, e->key, e->value);
    }

    /* Isolate module execution in the upper half of the host stack */
    MsValue* base = vm->host_stack + MS_STACK_SIZE / 2;
    vm->ctx->frame_count              = 1;
    vm->ctx->open_upvalues            = NULL;
    vm->exception_count          = 0;
    vm->ctx->frames[0].closure        = cl;
    vm->ctx->frames[0].ip             = fn->chunk.code;
    vm->ctx->frames[0].slots          = base;
    vm->ctx->frames[0].deferred       = NULL;
    vm->ctx->frames[0].deferred_count    = 0;
    vm->ctx->frames[0].deferred_capacity = 0;
    vm->ctx->stack_top = base + fn->max_stack_size + 1;

    MsInterpretResult r = vm_run_inner(vm);

    /* Everything the module defined (non-native) goes into mod->exports */
    if (r == MS_INTERPRET_OK) {
        for (int i = 0; i < vm->globals.capacity; i++) {
            MsEntry* e = &vm->globals.entries[i];
            if (e->key == NULL || e->key == MS_TABLE_TOMBSTONE) continue;
            if (!MS_IS_NATIVE(e->value))
                ms_table_set(&mod->exports, e->key, e->value);
        }
    }
    ms_table_free(&vm->globals);

    /* Restore host state and active context */
    vm->globals           = saved_globals;
    vm->ctx->frame_count   = saved_fc;
    vm->ctx->stack_top     = saved_top;
    vm->ctx->open_upvalues = saved_uv;
    vm->exception_count    = saved_exc;
    for (int i = 0; i < saved_fc; i++) vm->ctx->frames[i] = saved_frames[i];
    vm->ctx = saved_ctx;

    return r;
}

/* ---- main dispatch loop ---- */

#define RUNTIME_ERROR(vm, ...) \
    do { ms_vm_runtime_error(vm, __VA_ARGS__); \
         return MS_INTERPRET_RUNTIME_ERROR; } while (0)

static MsInterpretResult vm_run_inner(MsVM* vm) {
    MsCallFrame* frame = &vm->ctx->frames[vm->ctx->frame_count - 1];

#define READ_INSTR() (*frame->ip++)
#define K(idx)  (frame->closure->function->chunk.constants.data[idx])
#define RK(n)   (MS_RK_IS_K(n) ? K(MS_RK_TO_K(n)) : frame->slots[n])
#define R(n)    (frame->slots[n])

/* Computed-goto dispatch: GCC/Clang use a jump table; MSVC falls back to switch. */
#if defined(__GNUC__) || defined(__clang__)
#  define USE_COMPUTED_GOTO 1
#endif

#ifdef USE_COMPUTED_GOTO
    static const void* const dispatch_table[MS_OP_COUNT] = {
        &&L_MS_OP_LOADK, &&L_MS_OP_LOADNIL, &&L_MS_OP_LOADTRUE, &&L_MS_OP_LOADFALSE, &&L_MS_OP_MOVE,
        &&L_MS_OP_GETGLOBAL, &&L_MS_OP_SETGLOBAL, &&L_MS_OP_DEFGLOBAL,
        &&L_MS_OP_GETUPVAL, &&L_MS_OP_SETUPVAL,
        &&L_MS_OP_GETPROP, &&L_MS_OP_SETPROP, &&L_MS_OP_GETSUPER, &&L_MS_OP_EXTRAARG,
        &&L_MS_OP_ADD, &&L_MS_OP_SUB, &&L_MS_OP_MUL, &&L_MS_OP_DIV, &&L_MS_OP_MOD,
        &&L_MS_OP_EQ, &&L_MS_OP_LT, &&L_MS_OP_LE,
        &&L_MS_OP_NEG, &&L_MS_OP_NOT, &&L_MS_OP_STR,
        &&L_MS_OP_BAND, &&L_MS_OP_BOR, &&L_MS_OP_BXOR, &&L_MS_OP_BNOT, &&L_MS_OP_SHL, &&L_MS_OP_SHR,
        &&L_MS_OP_JMP, &&L_MS_OP_TEST, &&L_MS_OP_TESTSET,
        &&L_MS_OP_CALL, &&L_MS_OP_INVOKE, &&L_MS_OP_SUPERINV, &&L_MS_OP_RETURN,
        &&L_MS_OP_CLOSURE, &&L_MS_OP_CLOSE,
        &&L_MS_OP_CLASS, &&L_MS_OP_INHERIT, &&L_MS_OP_METHOD, &&L_MS_OP_STATICMETH,
        &&L_MS_OP_GETTER, &&L_MS_OP_SETTER, &&L_MS_OP_ABSTMETH,
        &&L_MS_OP_NEWLIST, &&L_MS_OP_NEWMAP, &&L_MS_OP_NEWTUPLE, &&L_MS_OP_GETIDX, &&L_MS_OP_SETIDX,
        &&L_MS_OP_IMPORT, &&L_MS_OP_IMPFROM, &&L_MS_OP_IMPALIAS,
        &&L_MS_OP_FORITER,
        &&L_MS_OP_THROW, &&L_MS_OP_TRY, &&L_MS_OP_ENDTRY,
        &&L_MS_OP_DEFER,
        &&L_MS_OP_YIELD, &&L_MS_OP_RESUME,
        &&L_MS_OP_AWAIT,
        &&L_MS_OP_ADD_II, &&L_MS_OP_ADD_FF, &&L_MS_OP_ADD_SS,
        &&L_MS_OP_SUB_II, &&L_MS_OP_SUB_FF,
        &&L_MS_OP_MUL_II, &&L_MS_OP_MUL_FF,
        &&L_MS_OP_DIV_FF,
        &&L_MS_OP_LT_II, &&L_MS_OP_LT_FF,
        &&L_MS_OP_EQ_II,
        &&L_MS_OP_NOP,
        &&L_MS_OP_ADD_RK, &&L_MS_OP_SUB_RK, &&L_MS_OP_MUL_RK, &&L_MS_OP_DIV_RK,
        &&L_MS_OP_LT_RK, &&L_MS_OP_LE_RK, &&L_MS_OP_EQ_RK,
        &&L_MS_OP_GETGLOBAL_CACHED,
    };
#  define VM_CASE(op) L_##op: case op:
#else
#  define VM_CASE(op) case op:
#endif

    for (;;) {
        MsInstruction instr = READ_INSTR();
        int op = MS_GET_OP(instr);
        int A  = MS_GET_A(instr);
        int B  = MS_GET_B(instr);
        int C  = MS_GET_C(instr);
#ifdef MSLANG_VM_STATS
        vm->stats.instruction_count++;
#endif

#ifdef USE_COMPUTED_GOTO
        goto *dispatch_table[op];
#endif
        switch (op) {
        VM_CASE(MS_OP_NOP) break;

        VM_CASE(MS_OP_LOADK)
            R(A) = K(MS_GET_Bx(instr));
            break;

        VM_CASE(MS_OP_LOADNIL) {
            int last = A + B;
            for (int i = A; i <= last; i++) frame->slots[i] = MS_NIL_VAL();
            break;
        }

        VM_CASE(MS_OP_LOADTRUE)  R(A) = MS_BOOL_VAL(true);  break;
        VM_CASE(MS_OP_LOADFALSE) R(A) = MS_BOOL_VAL(false); break;
        VM_CASE(MS_OP_MOVE)      R(A) = R(B);                break;

        VM_CASE(MS_OP_GETGLOBAL_CACHED)  /* v1: no caching; fall through to GETGLOBAL */
        VM_CASE(MS_OP_GETGLOBAL) {
            MsObjString* name = MS_AS_STRING(K(MS_GET_Bx(instr)));
            MsValue val;
            if (!ms_table_get(&vm->globals, name, &val)) {
                RUNTIME_ERROR(vm, "Undefined variable '%s'.", name->data);
            }
            R(A) = val;
            break;
        }

        VM_CASE(MS_OP_DEFGLOBAL)
        VM_CASE(MS_OP_SETGLOBAL) {
            MsObjString* name = MS_AS_STRING(K(MS_GET_Bx(instr)));
            ms_table_set(&vm->globals, name, R(A));
            break;
        }

        VM_CASE(MS_OP_GETUPVAL)
            R(A) = *frame->closure->upvalues[MS_GET_Bx(instr)]->location;
            break;

        VM_CASE(MS_OP_SETUPVAL)
            *frame->closure->upvalues[MS_GET_Bx(instr)]->location = R(A);
            ms_write_barrier(vm, (MsObject*)frame->closure, R(A));
            break;

        /* ---- generic arithmetic: execute + quicken ---- */
        VM_CASE(MS_OP_ADD) {
            MsValue bv = RK(B), cv = RK(C);
            MsValue result = MS_NIL_VAL();
            if (MS_IS_INT(bv) && MS_IS_INT(cv)) {
                result = MS_INT_VAL(MS_AS_INT(bv) + MS_AS_INT(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_ADD_II, A, B, C);
            } else if (MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv)) {
                result = MS_NUMBER_VAL(ms_as_double(bv) + ms_as_double(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_ADD_FF, A, B, C);
            } else if (MS_IS_STRING(bv) && MS_IS_STRING(cv)) {
                result = MS_OBJ_VAL(
                    ms_obj_string_concat(vm, MS_AS_STRING(bv), MS_AS_STRING(cv)));
                frame->ip[-1] = ms_enc_ABC(MS_OP_ADD_SS, A, B, C);
            } else {
                RUNTIME_ERROR(vm, "Operands must be numbers or strings for '+'.");
            }
            R(A) = result;
            break;
        }
        VM_CASE(MS_OP_SUB) {
            MsValue bv = RK(B), cv = RK(C);
            MsValue result = MS_NIL_VAL();
            if (MS_IS_INT(bv) && MS_IS_INT(cv)) {
                result = MS_INT_VAL(MS_AS_INT(bv) - MS_AS_INT(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_SUB_II, A, B, C);
            } else if (MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv)) {
                result = MS_NUMBER_VAL(ms_as_double(bv) - ms_as_double(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_SUB_FF, A, B, C);
            } else {
                RUNTIME_ERROR(vm, "Operands must be numbers for '-'.");
            }
            R(A) = result;
            break;
        }
        VM_CASE(MS_OP_MUL) {
            MsValue bv = RK(B), cv = RK(C);
            MsValue result = MS_NIL_VAL();
            if (MS_IS_INT(bv) && MS_IS_INT(cv)) {
                result = MS_INT_VAL(MS_AS_INT(bv) * MS_AS_INT(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_MUL_II, A, B, C);
            } else if (MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv)) {
                result = MS_NUMBER_VAL(ms_as_double(bv) * ms_as_double(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_MUL_FF, A, B, C);
            } else {
                RUNTIME_ERROR(vm, "Operands must be numbers for '*'.");
            }
            R(A) = result;
            break;
        }
        VM_CASE(MS_OP_DIV) {
            MsValue bv = RK(B), cv = RK(C);
            MsValue result = MS_NIL_VAL();
            if (MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv)) {
                result = MS_NUMBER_VAL(ms_as_double(bv) / ms_as_double(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_DIV_FF, A, B, C);
            } else {
                RUNTIME_ERROR(vm, "Operands must be numbers for '/'.");
            }
            R(A) = result;
            break;
        }
        VM_CASE(MS_OP_MOD) {
            MsValue bv = RK(B), cv = RK(C);
            MsValue result = MS_NIL_VAL();
            if (!numeric_binop(bv, cv, MS_OP_MOD, &result)) {
                RUNTIME_ERROR(vm, "Operands must be numbers for '%%'.");
            }
            R(A) = result;
            break;
        }

        /* ---- specialized arithmetic: fast path + deopt on mismatch ---- */
#define DEOPT_AND_RESPECIALIZE(generic_op, spec_ii, spec_ff) \
        { \
            int _off = (int)(frame->ip - 1 - frame->closure->function->chunk.code); \
            int _cnt = bump_deopt(frame->closure->function, _off); \
            if (_cnt >= 3) { \
                frame->ip[-1] = ms_enc_ABC((generic_op), A, B, C); \
            } else { \
                MsValue _bv = RK(B), _cv = RK(C); \
                if (MS_IS_INT(_bv) && MS_IS_INT(_cv)) \
                    frame->ip[-1] = ms_enc_ABC((spec_ii), A, B, C); \
                else if (MS_IS_NUMERIC(_bv) && MS_IS_NUMERIC(_cv)) \
                    frame->ip[-1] = ms_enc_ABC((spec_ff), A, B, C); \
                else \
                    frame->ip[-1] = ms_enc_ABC((generic_op), A, B, C); \
            } \
            STATS_TRACK_DEOPT(vm); \
        }

        VM_CASE(MS_OP_ADD_II) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_INT(bv) && MS_IS_INT(cv))) {
                R(A) = MS_INT_VAL(MS_AS_INT(bv) + MS_AS_INT(cv));
            } else {
                DEOPT_AND_RESPECIALIZE(MS_OP_ADD, MS_OP_ADD_II, MS_OP_ADD_FF)
                MsValue result = MS_NIL_VAL();
                if (MS_IS_STRING(bv) && MS_IS_STRING(cv))
                    result = MS_OBJ_VAL(ms_obj_string_concat(vm, MS_AS_STRING(bv), MS_AS_STRING(cv)));
                else if (!numeric_binop(bv, cv, MS_OP_ADD, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers or strings for '+'.");
                R(A) = result;
            }
            break;
        }
        VM_CASE(MS_OP_ADD_FF) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv))) {
                R(A) = MS_NUMBER_VAL(ms_as_double(bv) + ms_as_double(cv));
            } else {
                DEOPT_AND_RESPECIALIZE(MS_OP_ADD, MS_OP_ADD_II, MS_OP_ADD_FF)
                MsValue result = MS_NIL_VAL();
                if (MS_IS_STRING(bv) && MS_IS_STRING(cv))
                    result = MS_OBJ_VAL(ms_obj_string_concat(vm, MS_AS_STRING(bv), MS_AS_STRING(cv)));
                else if (!numeric_binop(bv, cv, MS_OP_ADD, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers or strings for '+'.");
                R(A) = result;
            }
            break;
        }
        VM_CASE(MS_OP_ADD_SS) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_STRING(bv) && MS_IS_STRING(cv))) {
                R(A) = MS_OBJ_VAL(ms_obj_string_concat(vm, MS_AS_STRING(bv), MS_AS_STRING(cv)));
            } else {
                int _off = (int)(frame->ip - 1 - frame->closure->function->chunk.code);
                int _cnt = bump_deopt(frame->closure->function, _off);
                if (_cnt >= 3)
                    frame->ip[-1] = ms_enc_ABC(MS_OP_ADD, A, B, C);
                else if (MS_IS_INT(bv) && MS_IS_INT(cv))
                    frame->ip[-1] = ms_enc_ABC(MS_OP_ADD_II, A, B, C);
                else if (MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv))
                    frame->ip[-1] = ms_enc_ABC(MS_OP_ADD_FF, A, B, C);
                else
                    frame->ip[-1] = ms_enc_ABC(MS_OP_ADD, A, B, C);
                STATS_TRACK_DEOPT(vm);
                MsValue result = MS_NIL_VAL();
                if (!numeric_binop(bv, cv, MS_OP_ADD, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers or strings for '+'.");
                R(A) = result;
            }
            break;
        }
        VM_CASE(MS_OP_SUB_II) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_INT(bv) && MS_IS_INT(cv))) {
                R(A) = MS_INT_VAL(MS_AS_INT(bv) - MS_AS_INT(cv));
            } else {
                DEOPT_AND_RESPECIALIZE(MS_OP_SUB, MS_OP_SUB_II, MS_OP_SUB_FF)
                MsValue result = MS_NIL_VAL();
                if (!numeric_binop(bv, cv, MS_OP_SUB, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers for '-'.");
                R(A) = result;
            }
            break;
        }
        VM_CASE(MS_OP_SUB_FF) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv))) {
                R(A) = MS_NUMBER_VAL(ms_as_double(bv) - ms_as_double(cv));
            } else {
                DEOPT_AND_RESPECIALIZE(MS_OP_SUB, MS_OP_SUB_II, MS_OP_SUB_FF)
                MsValue result = MS_NIL_VAL();
                if (!numeric_binop(bv, cv, MS_OP_SUB, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers for '-'.");
                R(A) = result;
            }
            break;
        }
        VM_CASE(MS_OP_MUL_II) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_INT(bv) && MS_IS_INT(cv))) {
                R(A) = MS_INT_VAL(MS_AS_INT(bv) * MS_AS_INT(cv));
            } else {
                DEOPT_AND_RESPECIALIZE(MS_OP_MUL, MS_OP_MUL_II, MS_OP_MUL_FF)
                MsValue result = MS_NIL_VAL();
                if (!numeric_binop(bv, cv, MS_OP_MUL, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers for '*'.");
                R(A) = result;
            }
            break;
        }
        VM_CASE(MS_OP_MUL_FF) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv))) {
                R(A) = MS_NUMBER_VAL(ms_as_double(bv) * ms_as_double(cv));
            } else {
                DEOPT_AND_RESPECIALIZE(MS_OP_MUL, MS_OP_MUL_II, MS_OP_MUL_FF)
                MsValue result = MS_NIL_VAL();
                if (!numeric_binop(bv, cv, MS_OP_MUL, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers for '*'.");
                R(A) = result;
            }
            break;
        }
        VM_CASE(MS_OP_DIV_FF) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv))) {
                R(A) = MS_NUMBER_VAL(ms_as_double(bv) / ms_as_double(cv));
            } else {
                int _off = (int)(frame->ip - 1 - frame->closure->function->chunk.code);
                int _cnt = bump_deopt(frame->closure->function, _off);
                if (_cnt >= 3)
                    frame->ip[-1] = ms_enc_ABC(MS_OP_DIV, A, B, C);
                else
                    frame->ip[-1] = ms_enc_ABC(MS_OP_DIV, A, B, C);
                STATS_TRACK_DEOPT(vm);
                MsValue result = MS_NIL_VAL();
                if (!numeric_binop(bv, cv, MS_OP_DIV, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers for '/'.");
                R(A) = result;
            }
            break;
        }
        VM_CASE(MS_OP_LT_II) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_INT(bv) && MS_IS_INT(cv))) {
                R(A) = MS_BOOL_VAL(MS_AS_INT(bv) < MS_AS_INT(cv));
            } else {
                int _off = (int)(frame->ip - 1 - frame->closure->function->chunk.code);
                int _cnt = bump_deopt(frame->closure->function, _off);
                MsOpCode spec = (_cnt >= 3 || (!MS_IS_NUMERIC(bv) || !MS_IS_NUMERIC(cv)))
                                ? MS_OP_LT
                                : (MS_IS_INT(bv) && MS_IS_INT(cv) ? MS_OP_LT_II : MS_OP_LT_FF);
                frame->ip[-1] = ms_enc_ABC(spec, A, B, C);
                STATS_TRACK_DEOPT(vm);
                bool result = false;
                if (!cmp_values(bv, cv, MS_OP_LT, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers for '<'.");
                R(A) = MS_BOOL_VAL(result);
            }
            break;
        }
        VM_CASE(MS_OP_LT_FF) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv))) {
                R(A) = MS_BOOL_VAL(ms_as_double(bv) < ms_as_double(cv));
            } else {
                int _off = (int)(frame->ip - 1 - frame->closure->function->chunk.code);
                int _cnt = bump_deopt(frame->closure->function, _off);
                MsOpCode spec = (_cnt >= 3 || (!MS_IS_NUMERIC(bv) || !MS_IS_NUMERIC(cv)))
                                ? MS_OP_LT
                                : (MS_IS_INT(bv) && MS_IS_INT(cv) ? MS_OP_LT_II : MS_OP_LT_FF);
                frame->ip[-1] = ms_enc_ABC(spec, A, B, C);
                STATS_TRACK_DEOPT(vm);
                bool result = false;
                if (!cmp_values(bv, cv, MS_OP_LT, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers for '<'.");
                R(A) = MS_BOOL_VAL(result);
            }
            break;
        }
        VM_CASE(MS_OP_EQ_II) {
            MsValue bv = RK(B), cv = RK(C);
            if (MS_LIKELY(MS_IS_INT(bv) && MS_IS_INT(cv))) {
                R(A) = MS_BOOL_VAL(MS_AS_INT(bv) == MS_AS_INT(cv));
            } else {
                int _off = (int)(frame->ip - 1 - frame->closure->function->chunk.code);
                int _cnt = bump_deopt(frame->closure->function, _off);
                if (_cnt >= 3)
                    frame->ip[-1] = ms_enc_ABC(MS_OP_EQ, A, B, C);
                else
                    frame->ip[-1] = ms_enc_ABC(MS_OP_EQ, A, B, C);
                STATS_TRACK_DEOPT(vm);
                bool result = false;
                cmp_values(bv, cv, MS_OP_EQ, &result);
                R(A) = MS_BOOL_VAL(result);
            }
            break;
        }
#undef DEOPT_AND_RESPECIALIZE

        /* RK-specialized opcodes: C is direct const-pool index, no RK_IS_K branch */
        VM_CASE(MS_OP_ADD_RK) {
            MsValue bv = R(B), cv = K(C);
            if (MS_IS_INT(bv) && MS_IS_INT(cv)) {
                R(A) = MS_INT_VAL(MS_AS_INT(bv) + MS_AS_INT(cv));
            } else if (MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv)) {
                R(A) = MS_NUMBER_VAL(ms_as_double(bv) + ms_as_double(cv));
            } else if (MS_IS_STRING(bv) && MS_IS_STRING(cv)) {
                R(A) = MS_OBJ_VAL(ms_obj_string_concat(vm, MS_AS_STRING(bv), MS_AS_STRING(cv)));
            } else {
                RUNTIME_ERROR(vm, "Operands must be numbers or strings for '+'.");
            }
            break;
        }
        VM_CASE(MS_OP_SUB_RK) {
            MsValue bv = R(B), cv = K(C);
            MsValue result = MS_NIL_VAL();
            if (!numeric_binop(bv, cv, MS_OP_SUB, &result))
                RUNTIME_ERROR(vm, "Operands must be numbers for '-'.");
            R(A) = result;
            break;
        }
        VM_CASE(MS_OP_MUL_RK) {
            MsValue bv = R(B), cv = K(C);
            MsValue result = MS_NIL_VAL();
            if (!numeric_binop(bv, cv, MS_OP_MUL, &result))
                RUNTIME_ERROR(vm, "Operands must be numbers for '*'.");
            R(A) = result;
            break;
        }
        VM_CASE(MS_OP_DIV_RK) {
            MsValue bv = R(B), cv = K(C);
            MsValue result = MS_NIL_VAL();
            if (!numeric_binop(bv, cv, MS_OP_DIV, &result))
                RUNTIME_ERROR(vm, "Operands must be numbers for '/'.");
            R(A) = result;
            break;
        }
        VM_CASE(MS_OP_LT_RK) {
            MsValue bv = R(B), cv = K(C);
            bool result = false;
            if (!cmp_values(bv, cv, MS_OP_LT, &result))
                RUNTIME_ERROR(vm, "Operands must be numbers for '<'.");
            R(A) = MS_BOOL_VAL(result);
            break;
        }
        VM_CASE(MS_OP_LE_RK) {
            MsValue bv = R(B), cv = K(C);
            bool result = false;
            if (!cmp_values(bv, cv, MS_OP_LE, &result))
                RUNTIME_ERROR(vm, "Operands must be numbers for '<='.");
            R(A) = MS_BOOL_VAL(result);
            break;
        }
        VM_CASE(MS_OP_EQ_RK) {
            MsValue bv = R(B), cv = K(C);
            bool result = false;
            cmp_values(bv, cv, MS_OP_EQ, &result);
            R(A) = MS_BOOL_VAL(result);
            break;
        }
        VM_CASE(MS_OP_NEG)
            if (MS_IS_INT(RK(B)))
                R(A) = MS_INT_VAL(-MS_AS_INT(RK(B)));
            else if (MS_IS_NUMBER(RK(B)))
                R(A) = MS_NUMBER_VAL(-MS_AS_NUMBER(RK(B)));
            else
                RUNTIME_ERROR(vm, "Operand must be a number.");
            break;

        VM_CASE(MS_OP_NOT)
            R(A) = MS_BOOL_VAL(!ms_value_is_truthy(RK(B)));
            break;

        VM_CASE(MS_OP_STR) {
            MsValue bv = RK(B);
            if (MS_IS_STRING(bv)) {
                R(A) = bv;
            } else {
                char* s = ms_value_to_cstring(bv);
                R(A) = MS_OBJ_VAL(ms_obj_string_take(vm, s, (int)strlen(s)));
            }
            break;
        }

        VM_CASE(MS_OP_BAND) {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Bitwise operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) & MS_AS_INT(cv));
            break;
        }
        VM_CASE(MS_OP_BOR) {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Bitwise operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) | MS_AS_INT(cv));
            break;
        }
        VM_CASE(MS_OP_BXOR) {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Bitwise operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) ^ MS_AS_INT(cv));
            break;
        }
        VM_CASE(MS_OP_BNOT) {
            MsValue bv = RK(B);
            if (!MS_IS_INT(bv))
                RUNTIME_ERROR(vm, "Bitwise operand must be integer.");
            R(A) = MS_INT_VAL(~MS_AS_INT(bv));
            break;
        }
        VM_CASE(MS_OP_SHL) {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Shift operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) << MS_AS_INT(cv));
            break;
        }
        VM_CASE(MS_OP_SHR) {
            MsValue bv = RK(B), cv = RK(C);
            if (!MS_IS_INT(bv) || !MS_IS_INT(cv))
                RUNTIME_ERROR(vm, "Shift operands must be integers.");
            R(A) = MS_INT_VAL(MS_AS_INT(bv) >> MS_AS_INT(cv));
            break;
        }

        VM_CASE(MS_OP_EQ) {
            MsValue bv = RK(B), cv = RK(C);
            bool result = false;
            if (MS_IS_INT(bv) && MS_IS_INT(cv)) {
                result = (MS_AS_INT(bv) == MS_AS_INT(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_EQ_II, A, B, C);
            } else {
                cmp_values(bv, cv, MS_OP_EQ, &result);
            }
            R(A) = MS_BOOL_VAL(result);
            break;
        }
        VM_CASE(MS_OP_LT) {
            MsValue bv = RK(B), cv = RK(C);
            bool result = false;
            if (MS_IS_INT(bv) && MS_IS_INT(cv)) {
                result = (MS_AS_INT(bv) < MS_AS_INT(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_LT_II, A, B, C);
            } else if (MS_IS_NUMERIC(bv) && MS_IS_NUMERIC(cv)) {
                result = (ms_as_double(bv) < ms_as_double(cv));
                frame->ip[-1] = ms_enc_ABC(MS_OP_LT_FF, A, B, C);
            } else {
                if (!cmp_values(bv, cv, MS_OP_LT, &result))
                    RUNTIME_ERROR(vm, "Operands must be numbers for comparison.");
            }
            R(A) = MS_BOOL_VAL(result);
            break;
        }
        VM_CASE(MS_OP_LE) {
            bool result = false;
            if (!cmp_values(RK(B), RK(C), MS_OP_LE, &result))
                RUNTIME_ERROR(vm, "Operands must be numbers for comparison.");
            R(A) = MS_BOOL_VAL(result);
            break;
        }

        VM_CASE(MS_OP_JMP) {
            int offset = MS_GET_sBx(instr);
            frame->ip += offset;
            break;
        }

        VM_CASE(MS_OP_TEST) {
            /* B=0: skip JMP when truthy (if/while/and); B=1: skip JMP when falsy (or) */
            bool truthy = ms_value_is_truthy(R(A));
            if ((bool)B != truthy) frame->ip++;
            break;
        }

        VM_CASE(MS_OP_TESTSET) {
            bool truthy = ms_value_is_truthy(R(B));
            if ((bool)C == truthy) {
                R(A) = R(B);
            } else {
                frame->ip++;
            }
            break;
        }

        VM_CASE(MS_OP_CALL) {
            /* A=callee reg, B=argc, C=first_arg_reg (unused by VM, args follow A) */
            MsInterpretResult cr = call_value(vm, R(A), B, A);
            if (cr != MS_INTERPRET_OK) return cr;
            /* refresh frame pointer after potential new frame push */
            frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
            break;
        }

        VM_CASE(MS_OP_CLOSURE) {
            MsObjFunction* proto = MS_AS_FUNCTION(K(MS_GET_Bx(instr)));
            MsObjClosure*  cl    = ms_obj_closure_new(vm, proto);
            R(A) = MS_OBJ_VAL(cl);
            for (int i = 0; i < proto->upvalue_count; i++) {
                MsInstruction ea = READ_INSTR();
                int is_local = MS_GET_A(ea);
                int idx      = MS_GET_Bx(ea);
                if (is_local) {
                    cl->upvalues[i] = capture_upvalue(vm, &frame->slots[idx]);
                } else {
                    cl->upvalues[i] = frame->closure->upvalues[idx];
                }
            }
            break;
        }

        VM_CASE(MS_OP_CLOSE)
            close_upvalues(vm, &frame->slots[A]);
            break;

        VM_CASE(MS_OP_RETURN) {
            /* B=0: implicit nil, B=1: nil, B>=2: return R(A) */
            MsValue ret = (B >= 2) ? R(A) : MS_NIL_VAL();
            /* init() must return the instance (slot 0), not nil */
            MsObjFunction* retfn = frame->closure->function;
            if (retfn->name == vm->init_string && MS_IS_NIL(ret)) {
                ret = frame->slots[0];
            }
            int cur_fi = vm->ctx->frame_count - 1;
            /* Pop any exception handlers belonging to this frame */
            while (vm->exception_count > 0 &&
                   vm->exception_handlers[vm->exception_count - 1].frame_index == cur_fi) {
                vm->exception_count--;
            }
            /* Run deferred closures LIFO before closing upvalues */
            run_deferred(vm, frame);
            close_upvalues(vm, frame->slots);
            vm->ctx->frame_count--;
            if (vm->ctx->frame_count == 0) {
                vm->ctx->stack_top    = vm->ctx->stack;
                vm->call_result  = ret;
                return MS_INTERPRET_OK;
            }

            /* Recover caller CALL target register */
            MsCallFrame* caller = &vm->ctx->frames[vm->ctx->frame_count - 1];
            MsInstruction call_instr = *(caller->ip - 1);
            int target = MS_GET_A(call_instr);
            caller->slots[target] = ret;
            frame = caller;
            break;
        }

        VM_CASE(MS_OP_EXTRAARG)
            /* consumed inline by CLOSURE; standalone is a no-op */
            break;

        VM_CASE(MS_OP_CLASS) {
            MsObjString* name = MS_AS_STRING(K(MS_GET_Bx(instr)));
            R(A) = MS_OBJ_VAL(ms_obj_class_new(vm, name));
            break;
        }

        VM_CASE(MS_OP_METHOD) {
            /* A=class_reg, B=closure_reg, C=name_k */
            MsObjClass* klass = MS_AS_CLASS(R(A));
            MsObjString* name = MS_AS_STRING(K(C));
            ms_table_set(&klass->methods, name, R(B));
            break;
        }

        VM_CASE(MS_OP_GETPROP) {
            /* Read the EXTRAARG that follows (IC slot index) */
            MsInstruction ea = READ_INSTR();
            int ic_idx = MS_GET_Bx(ea);
            MsValue obj = R(B);
            /* Handle class.staticMethod access */
            if (MS_IS_CLASS(obj)) {
                MsObjClass* klass = MS_AS_CLASS(obj);
                MsObjString* name = MS_AS_STRING(K(MS_RK_TO_K(C)));
                MsValue val;
                if (klass->static_methods &&
                    ms_table_get(klass->static_methods, name, &val)) {
                    R(A) = val;
                } else {
                    RUNTIME_ERROR(vm, "Undefined static method '%s'.", name->data);
                }
                break;
            }
            /* Handle module.export access */
            if (MS_IS_MODULE(obj)) {
                MsObjModule* mod = MS_AS_MODULE(obj);
                MsObjString* name = MS_AS_STRING(K(MS_RK_TO_K(C)));
                MsValue val;
                if (!ms_table_get(&mod->exports, name, &val)) {
                    RUNTIME_ERROR(vm, "Module '%s' has no export '%s'.",
                                  mod->name ? mod->name->data : "?", name->data);
                }
                R(A) = val;
                break;
            }
            if (!MS_IS_INSTANCE(obj)) {
                RUNTIME_ERROR(vm, "Only instances have properties.");
            }
            MsObjInstance* inst = MS_AS_INSTANCE(obj);
            MsObjString* name = MS_AS_STRING(K(MS_RK_TO_K(C)));
            /* Try IC hit for field access */
            MsInlineCache* getprop_ic =
                ensure_ic(vm, frame->closure->function, ic_idx);
            {
                int slot = ic_get_field_slot(getprop_ic, inst);
                if (slot >= 0) {
                    R(A) = inst_get_by_slot(inst, slot);
                    break;
                }
            }
            /* IC miss: slow path */
            {
                int slot = ms_shape_find_slot(inst->shape, name);
                if (slot >= 0) {
                    R(A) = inst_get_by_slot(inst, slot);
                    ic_update_field(getprop_ic, inst, slot);
                    break;
                }
            }
            MsValue val;
            if (inst->klass->getters &&
                       ms_table_get(inst->klass->getters, name, &val)) {
                /* Call getter: 0 args, receiver = inst */
                MsObjClosure* cl = MS_AS_CLOSURE(val);
                if (vm->ctx->frame_count >= MS_FRAMES_MAX) {
                    RUNTIME_ERROR(vm, "Stack overflow.");
                }
                MsCallFrame* nf = &vm->ctx->frames[vm->ctx->frame_count++];
                STATS_TRACK_FRAME(vm);
                nf->closure = cl;
                nf->ip      = cl->function->chunk.code;
                nf->slots   = frame->slots + A;
                nf->slots[0] = obj;
                MsValue* ntop = nf->slots + cl->function->max_stack_size + 1;
                if (ntop > vm->ctx->stack_top) vm->ctx->stack_top = ntop;
                frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
            } else if (ms_table_get(&inst->klass->methods, name, &val)) {
                MsObjBoundMethod* bm = ms_obj_bound_method_new(
                    vm, obj, MS_AS_CLOSURE(val));
                R(A) = MS_OBJ_VAL(bm);
            } else {
                RUNTIME_ERROR(vm, "Undefined property '%s'.", name->data);
            }
            break;
        }

        VM_CASE(MS_OP_SETPROP) {
            /* A=val_reg, B=obj_reg, C=name_rk; followed by EXTRAARG ic_slot */
            MsInstruction setprop_ea = READ_INSTR();
            int setprop_ic_idx = MS_GET_Bx(setprop_ea);
            MsValue obj = R(B);
            if (!MS_IS_INSTANCE(obj)) {
                RUNTIME_ERROR(vm, "Only instances have properties.");
            }
            MsObjInstance* inst = MS_AS_INSTANCE(obj);
            MsObjString* name = MS_AS_STRING(K(MS_RK_TO_K(C)));
            MsValue setter_fn;
            if (inst->klass->setters &&
                ms_table_get(inst->klass->setters, name, &setter_fn)) {
                /* Call setter with val as arg */
                MsObjClosure* cl = MS_AS_CLOSURE(setter_fn);
                if (vm->ctx->frame_count >= MS_FRAMES_MAX) {
                    RUNTIME_ERROR(vm, "Stack overflow.");
                }
                MsValue val = R(A);
                MsCallFrame* nf = &vm->ctx->frames[vm->ctx->frame_count++];
                STATS_TRACK_FRAME(vm);
                nf->closure = cl;
                nf->ip      = cl->function->chunk.code;
                nf->slots   = frame->slots + B;
                nf->slots[0] = obj;
                nf->slots[1] = val;
                MsValue* ntop = nf->slots + cl->function->max_stack_size + 1;
                if (ntop > vm->ctx->stack_top) vm->ctx->stack_top = ntop;
                frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
            } else {
                /* IC-aware field set with shape transition */
                MsInlineCache* setprop_ic =
                    ensure_ic(vm, frame->closure->function, setprop_ic_idx);
                int slot = -1;
                /* Check IC hit (existing field with known shape) */
                if (!setprop_ic->megamorphic) {
                    for (int i = 0; i < setprop_ic->count; i++) {
                        if (setprop_ic->entries[i].kind == MS_IC_FIELD &&
                            setprop_ic->entries[i].shape_id == inst->shape->id) {
                            slot = (int)setprop_ic->entries[i].slot_index;
                            break;
                        }
                    }
                }
                if (slot < 0) {
                    /* Slow path: find existing slot or create via transition */
                    slot = ms_shape_find_slot(inst->shape, name);
                    if (slot < 0) {
                        /* New property: transition the shape */
                        inst->shape = ms_shape_transition(vm, inst->shape, name);
                        slot = (int)(inst->shape->slot_count - 1);
                    }
                    ic_update_field(setprop_ic, inst, slot);
                }
                inst_set_by_slot(vm, inst, slot, R(A));
                ms_write_barrier(vm, (MsObject*)inst, R(A));
            }
            break;
        }

        VM_CASE(MS_OP_INVOKE) {
            /* A=obj_reg, B=name_k, C=argc; followed by EXTRAARG ic_slot */
            MsInstruction invoke_ea = READ_INSTR();
            int invoke_ic_idx = MS_GET_Bx(invoke_ea);
            MsValue obj = R(A);
            /* Handle class.staticMethod() calls */
            if (MS_IS_CLASS(obj)) {
                MsObjClass* klass = MS_AS_CLASS(obj);
                MsObjString* name = MS_AS_STRING(K(B));
                MsValue smethod;
                if (klass->static_methods &&
                    ms_table_get(klass->static_methods, name, &smethod)) {
                    MsObjClosure* cl = MS_AS_CLOSURE(smethod);
                    MsObjFunction* fn = cl->function;
                    if (vm->ctx->frame_count >= MS_FRAMES_MAX) {
                        RUNTIME_ERROR(vm, "Stack overflow.");
                    }
                    MsCallFrame* nf = &vm->ctx->frames[vm->ctx->frame_count++];
                    STATS_TRACK_FRAME(vm);
                    nf->closure = cl;
                    nf->ip      = fn->chunk.code;
                    /* slot 0 = class (this), args in slots 1..argc */
                    nf->slots   = frame->slots + A;
                    MsValue* ntop = nf->slots + fn->max_stack_size + 1;
                    if (ntop > vm->ctx->stack_top) vm->ctx->stack_top = ntop;
                    frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
                } else {
                    RUNTIME_ERROR(vm, "Undefined static method '%s'.", name->data);
                }
                break;
            }
            /* Handle module.fn() calls */
            if (MS_IS_MODULE(obj)) {
                MsObjModule* mod = MS_AS_MODULE(obj);
                MsObjString* name = MS_AS_STRING(K(B));
                MsValue fn_val;
                if (!ms_table_get(&mod->exports, name, &fn_val)) {
                    RUNTIME_ERROR(vm, "Module '%s' has no export '%s'.",
                                  mod->name ? mod->name->data : "?", name->data);
                }
                MsInterpretResult cr = call_value(vm, fn_val, C, A);
                if (cr != MS_INTERPRET_OK) return cr;
                frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
                break;
            }
            if (!MS_IS_INSTANCE(obj)) {
                MsObjString* bname = MS_AS_STRING(K(B));
                /* argv starts at slot A+1 */
                MsValue* bargs = frame->slots + A + 1;
                MsValue bout  = MS_NIL_VAL();
                if (ms_builtin_invoke(vm, obj, bname, C, bargs, &bout)) {
                    R(A) = bout;
                    break;
                }
                RUNTIME_ERROR(vm, "No built-in method '%s' on this type.", bname->data);
            }
            MsObjInstance* inst = MS_AS_INSTANCE(obj);
            MsObjString* name = MS_AS_STRING(K(B));
            MsValue method = MS_NIL_VAL();
            /* IC lookup for instance methods */
            MsInlineCache* invoke_ic =
                ensure_ic(vm, frame->closure->function, invoke_ic_idx);
            if (!invoke_ic->megamorphic) {
                for (int i = 0; i < invoke_ic->count; i++) {
                    MsICEntry* e = &invoke_ic->entries[i];
                    if (e->kind == MS_IC_METHOD &&
                        e->shape_id == inst->shape->id) {
                        method = e->cached;
                        break;
                    }
                }
            }
            if (!MS_IS_CLOSURE(method)) {
                /* IC miss: slow path - field first, then class methods */
                int fslot = ms_shape_find_slot(inst->shape, name);
                if (fslot >= 0) {
                    method = inst_get_by_slot(inst, fslot);
                }
                if (!MS_IS_CLOSURE(method)) {
                    ms_table_get(&inst->klass->methods, name, &method);
                }
                if (!MS_IS_CLOSURE(method)) {
                    RUNTIME_ERROR(vm, "Undefined method '%s'.", name->data);
                }
                /* Fill IC cache */
                if (!invoke_ic->megamorphic) {
                    if (invoke_ic->count < MS_IC_PIC_SIZE) {
                        MsICEntry* e = &invoke_ic->entries[invoke_ic->count++];
                        e->kind       = MS_IC_METHOD;
                        e->shape_id   = inst->shape->id;
                        e->slot_index = 0; /* unused for METHOD kind */
                        e->cached     = method;
                    } else {
                        invoke_ic->megamorphic = true;
                    }
                }
            }
            /* Dispatch: slot 0 = receiver (this), args in slots 1..argc */
            {
                MsObjClosure* cl = MS_AS_CLOSURE(method);
                MsObjFunction* fn = cl->function;
                if (vm->ctx->frame_count >= MS_FRAMES_MAX) {
                    RUNTIME_ERROR(vm, "Stack overflow.");
                }
                MsCallFrame* nf = &vm->ctx->frames[vm->ctx->frame_count++];
                STATS_TRACK_FRAME(vm);
                nf->closure = cl;
                nf->ip      = fn->chunk.code;
                nf->slots   = frame->slots + A;
                MsValue* ntop = nf->slots + fn->max_stack_size + 1;
                if (ntop > vm->ctx->stack_top) vm->ctx->stack_top = ntop;
                frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
            }
            break;
        }

        VM_CASE(MS_OP_INHERIT) {
            /* A=subclass_reg, B=superclass_reg */
            MsValue super_val = R(B);
            if (!MS_IS_CLASS(super_val)) {
                RUNTIME_ERROR(vm, "Superclass must be a class.");
            }
            MsObjClass* sub   = MS_AS_CLASS(R(A));
            MsObjClass* super = MS_AS_CLASS(super_val);
            ms_table_add_all(&sub->methods, &super->methods);
            sub->superclass = super;
            break;
        }

        VM_CASE(MS_OP_GETSUPER) {
            /* A=result_reg, B=super_upval_idx, C=name_k */
            MsValue super_val = *frame->closure->upvalues[B]->location;
            if (!MS_IS_CLASS(super_val)) {
                RUNTIME_ERROR(vm, "Superclass is not a class.");
            }
            MsObjClass* super = MS_AS_CLASS(super_val);
            MsObjString* name = MS_AS_STRING(K(C));
            MsValue method;
            if (!ms_table_get(&super->methods, name, &method)) {
                RUNTIME_ERROR(vm, "Undefined super method '%s'.", name->data);
            }
            /* Bind to 'this' (slot 0) */
            MsObjBoundMethod* bm = ms_obj_bound_method_new(
                vm, frame->slots[0], MS_AS_CLOSURE(method));
            R(A) = MS_OBJ_VAL(bm);
            break;
        }

        VM_CASE(MS_OP_SUPERINV) {
            /* A=this_reg, B=super_upval_idx, C=name_k; argc in EXTRAARG */
            MsInstruction ea = READ_INSTR();
            int argc = MS_GET_A(ea);
            MsValue super_val = *frame->closure->upvalues[B]->location;
            if (!MS_IS_CLASS(super_val)) {
                RUNTIME_ERROR(vm, "Superclass is not a class.");
            }
            MsObjClass* super = MS_AS_CLASS(super_val);
            MsObjString* name = MS_AS_STRING(K(C));
            MsValue method;
            if (!ms_table_get(&super->methods, name, &method)) {
                RUNTIME_ERROR(vm, "Undefined super method '%s'.", name->data);
            }
            MsObjClosure* cl = MS_AS_CLOSURE(method);
            MsObjFunction* fn = cl->function;
            if (vm->ctx->frame_count >= MS_FRAMES_MAX) {
                RUNTIME_ERROR(vm, "Stack overflow.");
            }
            MsCallFrame* nf = &vm->ctx->frames[vm->ctx->frame_count++];
            STATS_TRACK_FRAME(vm);
            nf->closure = cl;
            nf->ip      = fn->chunk.code;
            nf->slots   = frame->slots + A;
            /* slot 0 is already 'this' (R(A)) */
            MsValue* ntop = nf->slots + fn->max_stack_size + 1;
            if (ntop > vm->ctx->stack_top) vm->ctx->stack_top = ntop;
            (void)argc;
            frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
            break;
        }

        VM_CASE(MS_OP_STATICMETH) {
            /* A=class_reg, B=closure_reg, C=name_k */
            MsObjClass* klass = MS_AS_CLASS(R(A));
            MsObjString* name = MS_AS_STRING(K(C));
            if (!klass->static_methods) {
                klass->static_methods = (MsTable*)malloc(sizeof(MsTable));
                ms_table_init(klass->static_methods);
            }
            ms_table_set(klass->static_methods, name, R(B));
            break;
        }

        VM_CASE(MS_OP_GETTER) {
            /* A=class_reg, B=closure_reg, C=name_k */
            MsObjClass* klass = MS_AS_CLASS(R(A));
            MsObjString* name = MS_AS_STRING(K(C));
            if (!klass->getters) {
                klass->getters = (MsTable*)malloc(sizeof(MsTable));
                ms_table_init(klass->getters);
            }
            ms_table_set(klass->getters, name, R(B));
            break;
        }

        VM_CASE(MS_OP_SETTER) {
            /* A=class_reg, B=closure_reg, C=name_k */
            MsObjClass* klass = MS_AS_CLASS(R(A));
            MsObjString* name = MS_AS_STRING(K(C));
            if (!klass->setters) {
                klass->setters = (MsTable*)malloc(sizeof(MsTable));
                ms_table_init(klass->setters);
            }
            ms_table_set(klass->setters, name, R(B));
            break;
        }

        VM_CASE(MS_OP_ABSTMETH) {
            /* A=class_reg, C=name_k: store nil sentinel in abstract_methods */
            MsObjClass* klass = MS_AS_CLASS(R(A));
            MsObjString* name = MS_AS_STRING(K(C));
            if (!klass->abstract_methods) {
                klass->abstract_methods = (MsTable*)malloc(sizeof(MsTable));
                ms_table_init(klass->abstract_methods);
            }
            ms_table_set(klass->abstract_methods, name, MS_NIL_VAL());
            break;
        }

        VM_CASE(MS_OP_NEWLIST) {
            /* A=dest_reg, B=count; elements in R(A)..R(A+B-1) */
            int count = MS_GET_B(instr);
            MsObjList* list = ms_obj_list_new(vm);
            /* push list to stack so GC can find it while we build */
            R(A) = MS_OBJ_VAL(list);
            for (int i = 0; i < count; i++)
                ms_value_array_push(&list->items, R(A + i + 1));
            /* shift: if elements were after A, they stay; result is in R(A) */
            break;
        }

        VM_CASE(MS_OP_NEWMAP) {
            /* A=dest_reg, B=pair_count; pairs in R(A+1),R(A+2),R(A+3),R(A+4)... */
            int pairs = MS_GET_B(instr);
            MsObjMap* map = ms_obj_map_new(vm);
            R(A) = MS_OBJ_VAL(map);
            for (int i = 0; i < pairs; i++) {
                MsValue key = R(A + 1 + i * 2);
                MsValue val = R(A + 2 + i * 2);
                ms_vtable_set(&map->table, key, val);
            }
            break;
        }

        VM_CASE(MS_OP_NEWTUPLE) {
            /* A=dest_reg, B=count; elements in R(A+1)..R(A+B) */
            int count = MS_GET_B(instr);
            MsValue* items = &frame->slots[A + 1];
            MsObjTuple* tup = ms_obj_tuple_new(vm, items, count);
            R(A) = MS_OBJ_VAL(tup);
            break;
        }

        VM_CASE(MS_OP_GETIDX) {
            /* A=dest, B=obj_reg, C=idx_reg_or_k */
            MsValue obj = R(B);
            MsValue idx = RK(C);
            if (MS_IS_LIST(obj)) {
                if (!MS_IS_INT(idx)) { RUNTIME_ERROR(vm, "List index must be integer."); }
                MsObjList* list = MS_AS_LIST(obj);
                int i = (int)MS_AS_INT(idx);
                if (i < 0) i += list->items.count;
                if (i < 0 || i >= list->items.count) { RUNTIME_ERROR(vm, "List index out of range."); }
                R(A) = list->items.data[i];
            } else if (MS_IS_TUPLE(obj)) {
                if (!MS_IS_INT(idx)) { RUNTIME_ERROR(vm, "Tuple index must be integer."); }
                MsObjTuple* tup = MS_AS_TUPLE(obj);
                int i = (int)MS_AS_INT(idx);
                if (i < 0) i += tup->count;
                if (i < 0 || i >= tup->count) { RUNTIME_ERROR(vm, "Tuple index out of range."); }
                R(A) = tup->items[i];
            } else if (MS_IS_MAP(obj)) {
                MsObjMap* map = MS_AS_MAP(obj);
                MsValue val = MS_NIL_VAL();
                ms_vtable_get(&map->table, idx, &val);
                R(A) = val;
            } else if (MS_IS_STRING(obj)) {
                if (!MS_IS_INT(idx)) { RUNTIME_ERROR(vm, "String index must be integer."); }
                MsObjString* s = MS_AS_STRING(obj);
                int i = (int)MS_AS_INT(idx);
                if (i < 0) i += s->length;
                if (i < 0 || i >= s->length) { RUNTIME_ERROR(vm, "String index out of range."); }
                R(A) = MS_OBJ_VAL(ms_obj_string_copy(vm, &s->data[i], 1));
            } else {
                RUNTIME_ERROR(vm, "Value is not indexable.");
            }
            break;
        }

        VM_CASE(MS_OP_SETIDX) {
            /* A=obj_reg, B=idx_reg_or_k, C=val_reg */
            MsValue obj = R(A);
            MsValue idx = RK(B);
            MsValue val = R(C);
            if (MS_IS_LIST(obj)) {
                if (!MS_IS_INT(idx)) { RUNTIME_ERROR(vm, "List index must be integer."); }
                MsObjList* list = MS_AS_LIST(obj);
                int i = (int)MS_AS_INT(idx);
                if (i < 0) i += list->items.count;
                if (i < 0 || i >= list->items.count) { RUNTIME_ERROR(vm, "List index out of range."); }
                list->items.data[i] = val;
            } else if (MS_IS_MAP(obj)) {
                ms_vtable_set(&MS_AS_MAP(obj)->table, idx, val);
            } else {
                RUNTIME_ERROR(vm, "Value is not subscript-assignable.");
            }
            break;
        }

        VM_CASE(MS_OP_TRY) {
            if (vm->exception_count >= MS_MAX_EXCEPTION_HANDLERS) {
                RUNTIME_ERROR(vm, "Exception handler stack overflow.");
            }
            int offset = MS_GET_sBx(instr);
            MsExceptionHandler* h = &vm->exception_handlers[vm->exception_count++];
            h->handler_ip  = frame->ip + offset;
            h->frame_index = vm->ctx->frame_count - 1;
            h->catch_reg   = A;
            h->stack_top   = vm->ctx->stack_top;
            break;
        }

        VM_CASE(MS_OP_ENDTRY)
            if (vm->exception_count > 0) vm->exception_count--;
            break;

        VM_CASE(MS_OP_THROW) {
            MsValue error = R(A);
            if (!throw_exception(vm, error)) {
                char* s = ms_value_to_cstring(error);
                ms_vm_runtime_error(vm, "Uncaught exception: %s", s);
                free(s);
                return MS_INTERPRET_RUNTIME_ERROR;
            }
            /* throw_exception updated frame/ip; refresh frame pointer */
            frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
            break;
        }

        VM_CASE(MS_OP_DEFER) {
            MsObjClosure* cl = MS_AS_CLOSURE(R(A));
            if (frame->deferred_count >= frame->deferred_capacity) {
                int cap = frame->deferred_capacity < 4 ? 4 : frame->deferred_capacity * 2;
                frame->deferred = (MsObjClosure**)realloc(
                    frame->deferred, sizeof(MsObjClosure*) * (size_t)cap);
                frame->deferred_capacity = cap;
            }
            frame->deferred[frame->deferred_count++] = cl;
            break;
        }

        VM_CASE(MS_OP_YIELD) {
            /* Must be inside a coroutine */
            MsObjCoroutine* co = vm->current_coroutine;
            if (!co) {
                RUNTIME_ERROR(vm, "yield outside coroutine.");
            }
            /* vm->ctx == &co->ctx; state is already live in co->ctx.
               Just record the yield value and mark suspended. */
            co->yield_value = (B >= 2) ? R(A) : MS_NIL_VAL();
            co->state       = MS_CORO_SUSPENDED;
            return MS_INTERPRET_YIELD;
        }

        VM_CASE(MS_OP_RESUME) {
            /* A=dst, B=coroutine_reg, C=sent_value_rk */
            MsValue coro_val = R(B);
            if (!MS_IS_COROUTINE(coro_val)) {
                RUNTIME_ERROR(vm, "resume: expected coroutine.");
            }
            MsObjCoroutine* co = MS_AS_COROUTINE(coro_val);
            if (co->state == MS_CORO_DEAD) {
                RUNTIME_ERROR(vm, "Cannot resume dead coroutine.");
            }
            if (co->state == MS_CORO_RUNNING) {
                RUNTIME_ERROR(vm, "Cannot resume running coroutine.");
            }
            MsValue sent = RK(C);
            MsValue result = MS_NIL_VAL();
            MsInterpretResult cr = ms_vm_coro_resume(vm, co, sent, &result);
            if (cr != MS_INTERPRET_OK) return MS_INTERPRET_RUNTIME_ERROR;
            /* Refresh frame pointer after state restore */
            frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
            R(A) = result;
            break;
        }

        VM_CASE(MS_OP_AWAIT) {
            /* A=result_reg, B=future_reg */
            MsValue fval = R(B);
            if (!MS_IS_FUTURE(fval)) {
                RUNTIME_ERROR(vm, "await: expected Future.");
            }
            MsObjFuture* fut = MS_AS_FUTURE(fval);
            if (fut->state == MS_FUTURE_RESOLVED) {
                R(A) = fut->result;   /* fast path: already resolved */
                break;
            }
            if (fut->state == MS_FUTURE_REJECTED) {
                if (!throw_exception(vm, fut->result))
                    return MS_INTERPRET_RUNTIME_ERROR;
                /* Refresh frame after exception dispatch */
                frame = &vm->ctx->frames[vm->ctx->frame_count - 1];
                break;
            }
            /* PENDING: register waiter, suspend coroutine */
            MsObjCoroutine* coro = vm->current_coroutine;
            if (!coro) {
                RUNTIME_ERROR(vm, "await outside async context.");
            }
            MsWaiter* w = (MsWaiter*)malloc(sizeof(MsWaiter));
            if (!w) { RUNTIME_ERROR(vm, "out of memory in await."); }
            w->type               = MS_WAITER_CORO;
            w->u.coro.coro        = coro;
            w->u.coro.frame_index = coro->ctx.frame_count - 1;
            w->u.coro.result_reg  = A;
            w->next               = fut->waiters;
            fut->waiters          = w;
            coro->state           = MS_CORO_SUSPENDED;
            return MS_INTERPRET_AWAIT;
        }

        VM_CASE(MS_OP_IMPORT) {
            /* A=dst_reg, Bx=path_const */
            MsObjString* path_str = MS_AS_STRING(K(MS_GET_Bx(instr)));
            /* from_path: the script path of the importing function */
            const char* from_path = frame->closure->function->script_path
                                    ? frame->closure->function->script_path->data
                                    : NULL;
            MsObjModule* mod = ms_module_load(vm, path_str->data, from_path);
            if (!mod) return MS_INTERPRET_RUNTIME_ERROR;
            R(A) = MS_OBJ_VAL(mod);
            break;
        }

        VM_CASE(MS_OP_IMPFROM) {
            /* A=dst_reg, B=mod_reg, C=name_const */
            MsValue mod_val = R(B);
            if (!MS_IS_MODULE(mod_val)) {
                RUNTIME_ERROR(vm, "IMPFROM: expected module.");
            }
            MsObjModule* mod = MS_AS_MODULE(mod_val);
            MsObjString* name = MS_AS_STRING(K(C));
            MsValue val;
            if (!ms_table_get(&mod->exports, name, &val)) {
                RUNTIME_ERROR(vm, "Module '%s' has no export '%s'.",
                              mod->name ? mod->name->data : "?", name->data);
            }
            R(A) = val;
            break;
        }

        VM_CASE(MS_OP_IMPALIAS)
            /* A=dst_reg, B=src_reg: just a move; alias already in dst */
            R(A) = R(B);
            break;

        VM_CASE(MS_OP_FORITER) {
            /* iAsBx: R(A)=iterable, R(A+1)=int-index, R(A+2)=elem out; sBx=exit offset */
            int sBx     = MS_GET_sBx(instr);
            MsValue seq = R(A);
            int     idx = (int)MS_AS_INT(R(A + 1));
            if (MS_IS_LIST(seq)) {
                MsObjList* list = MS_AS_LIST(seq);
                if (idx >= list->items.count) { frame->ip += sBx; break; }
                R(A + 2) = list->items.data[idx];
                R(A + 1) = MS_INT_VAL(idx + 1);
            } else if (MS_IS_TUPLE(seq)) {
                MsObjTuple* tup = MS_AS_TUPLE(seq);
                if (idx >= tup->count) { frame->ip += sBx; break; }
                R(A + 2) = tup->items[idx];
                R(A + 1) = MS_INT_VAL(idx + 1);
            } else if (MS_IS_STRING(seq)) {
                MsObjString* str = MS_AS_STRING(seq);
                if (idx >= str->length) { frame->ip += sBx; break; }
                R(A + 2) = MS_OBJ_VAL(ms_obj_string_copy(vm, &str->data[idx], 1));
                R(A + 1) = MS_INT_VAL(idx + 1);
            } else if (MS_IS_MAP(seq)) {
                MsObjMap* map = MS_AS_MAP(seq);
                while (idx < map->table.capacity &&
                       (!map->table.entries[idx].used || map->table.entries[idx].tombstone))
                    idx++;
                if (idx >= map->table.capacity) { frame->ip += sBx; break; }
                R(A + 2) = map->table.entries[idx].key;
                R(A + 1) = MS_INT_VAL(idx + 1);
            } else {
                RUNTIME_ERROR(vm, "Can only iterate over list, tuple, string, or map.");
            }
            break;
        }

        default:
            /* unimplemented opcode: store nil and continue */
            R(A) = MS_NIL_VAL();
            break;
        }
    }

#undef READ_INSTR
#undef K
#undef RK
#undef R
}
