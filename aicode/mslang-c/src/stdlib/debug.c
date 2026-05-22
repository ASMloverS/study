/* src/stdlib/debug.c - STDLIB-09: debug module
 * Provides runtime introspection: traceback, frame_info, locals, upvalues,
 * disasm, type predicates, vm_stats, gc_trace.
 */
#include "ms/stdlib_register.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/debug.h"
#include "ms/vtable.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ================================================================
 * Helpers
 * ================================================================ */

/* Return the active frame count and frame array from vm->ctx. */
static int active_frame_count(MsVM* vm) {
    return vm->ctx->frame_count;
}

static MsCallFrame* active_frame(MsVM* vm, int i) {
    return &vm->ctx->frames[i];
}

/* Map set helper: set string key → value in an ObjMap */
static void map_set_str(MsVM* vm, MsObjMap* map,
                        const char* key, MsValue val) {
    MsObjString* k = ms_obj_string_copy(vm, key, (int)strlen(key));
    ms_vtable_set(&map->table, MS_OBJ_VAL(k), val);
}

/* ================================================================
 * debug.traceback(depth=0) -> str
 * ================================================================ */

static MsValue ms_debug_traceback(MsVM* vm, int argc, MsValue* argv) {
    int skip = 0;
    if (argc >= 1 && MS_IS_INT(argv[0]))
        skip = (int)MS_AS_INT(argv[0]);

    int fc = active_frame_count(vm);
    MsObjStringBuilder* sb = ms_obj_sb_new(vm);
    char buf[512];

    for (int i = fc - 1 - skip; i >= 0; i--) {
        MsCallFrame*    frame = active_frame(vm, i);
        MsObjFunction*  fn    = frame->closure->function;
        int offset = (frame->ip > fn->chunk.code)
                     ? (int)(frame->ip - fn->chunk.code - 1)
                     : 0;
        int line = ms_chunk_get_line(&fn->chunk, offset);
        const char* fname = fn->name ? fn->name->data : "<script>";
        const char* fpath = fn->script_path ? fn->script_path->data : "?";
        int n = snprintf(buf, sizeof(buf), "  at %s (%s:%d)\n",
                         fname, fpath, line);
        if (n > 0) ms_obj_sb_append(sb, buf, n);
    }

    return MS_OBJ_VAL(ms_obj_sb_to_string(vm, sb));
}

/* ================================================================
 * debug.frame_info(depth=0) -> map {name, file, line}
 * ================================================================ */

static MsValue ms_debug_frame_info(MsVM* vm, int argc, MsValue* argv) {
    int depth = 0;
    if (argc >= 1 && MS_IS_INT(argv[0]))
        depth = (int)MS_AS_INT(argv[0]);

    int fc  = active_frame_count(vm);
    int idx = fc - 1 - depth;
    if (idx < 0 || idx >= fc) {
        ms_vm_runtime_error(vm, "debug.frame_info: depth out of range");
        return MS_NIL_VAL();
    }

    MsCallFrame*   frame = active_frame(vm, idx);
    MsObjFunction* fn    = frame->closure->function;
    int offset = (frame->ip > fn->chunk.code)
                 ? (int)(frame->ip - fn->chunk.code - 1)
                 : 0;
    int line = ms_chunk_get_line(&fn->chunk, offset);

    MsObjMap* map = ms_obj_map_new(vm);
    const char* fname = fn->name ? fn->name->data : "<script>";
    const char* fpath = fn->script_path ? fn->script_path->data : "?";
    map_set_str(vm, map, "name", MS_OBJ_VAL(ms_obj_string_copy(vm, fname, (int)strlen(fname))));
    map_set_str(vm, map, "file", MS_OBJ_VAL(ms_obj_string_copy(vm, fpath, (int)strlen(fpath))));
    map_set_str(vm, map, "line", MS_INT_VAL((ms_i64)line));
    return MS_OBJ_VAL(map);
}

/* ================================================================
 * debug.locals(depth=0) -> map  slot_N -> value snapshot
 * ================================================================ */

static MsValue ms_debug_locals(MsVM* vm, int argc, MsValue* argv) {
    int depth = 0;
    if (argc >= 1 && MS_IS_INT(argv[0]))
        depth = (int)MS_AS_INT(argv[0]);

    int fc  = active_frame_count(vm);
    int idx = fc - 1 - depth;
    if (idx < 0 || idx >= fc) {
        ms_vm_runtime_error(vm, "debug.locals: depth out of range");
        return MS_NIL_VAL();
    }

    MsCallFrame*   frame = active_frame(vm, idx);
    MsObjFunction* fn    = frame->closure->function;

    /* Slots 0..arity-1 are arguments; arity..max_stack_size-1 are locals.
       We expose all live slots the frame has allocated. */
    MsValue* base  = frame->slots;
    MsValue* top   = vm->ctx->stack_top;
    int      count = (idx == fc - 1)
                     ? (int)(top - base)
                     : (int)(active_frame(vm, idx + 1)->slots - base);
    /* Clamp to function's declared register window */
    int max_slots = fn->max_stack_size > 0 ? fn->max_stack_size : count;
    if (count > max_slots) count = max_slots;

    MsObjMap* map = ms_obj_map_new(vm);
    char key[32];
    for (int s = 0; s < count; s++) {
        int n = snprintf(key, sizeof(key), "slot_%d", s);
        MsObjString* k = ms_obj_string_copy(vm, key, n);
        ms_vtable_set(&map->table, MS_OBJ_VAL(k), base[s]);
    }
    return MS_OBJ_VAL(map);
}

/* ================================================================
 * debug.upvalues(closure) -> list of {name, value}
 * ================================================================ */

static MsValue ms_debug_upvalues(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_CLOSURE(argv[0])) {
        ms_vm_runtime_error(vm, "debug.upvalues: expected Closure");
        return MS_NIL_VAL();
    }
    MsObjClosure* cl = MS_AS_CLOSURE(argv[0]);
    MsObjList*    ls = ms_obj_list_new(vm);

    for (int i = 0; i < cl->upvalue_count; i++) {
        MsObjMap* entry = ms_obj_map_new(vm);
        char name_buf[32];
        snprintf(name_buf, sizeof(name_buf), "upval_%d", i);
        map_set_str(vm, entry, "name",
            MS_OBJ_VAL(ms_obj_string_copy(vm, name_buf, (int)strlen(name_buf))));
        MsValue val = cl->upvalues[i] ? *cl->upvalues[i]->location : MS_NIL_VAL();
        map_set_str(vm, entry, "value", val);
        ms_value_array_push(&ls->items, MS_OBJ_VAL(entry));
    }
    return MS_OBJ_VAL(ls);
}

/* ================================================================
 * debug.disasm(fn) -> str
 * ================================================================ */

static MsValue ms_debug_disasm(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) {
        ms_vm_runtime_error(vm, "debug.disasm: expected Function or Closure");
        return MS_NIL_VAL();
    }
    MsValue fv = argv[0];
    MsObjFunction* fn = NULL;
    if (MS_IS_OBJ_TYPE(fv, MS_OBJ_FUNCTION))
        fn = (MsObjFunction*)MS_AS_OBJECT(fv);
    else if (MS_IS_OBJ_TYPE(fv, MS_OBJ_CLOSURE))
        fn = ((MsObjClosure*)MS_AS_OBJECT(fv))->function;

    if (!fn) {
        ms_vm_runtime_error(vm, "debug.disasm: expected Function or Closure");
        return MS_NIL_VAL();
    }
    const char* name = fn->name ? fn->name->data : "<fn>";
    char* raw = ms_chunk_disassemble_str(&fn->chunk, name);
    if (!raw) return MS_OBJ_VAL(ms_obj_string_copy(vm, "", 0));
    MsObjString* s = ms_obj_string_copy(vm, raw, (int)strlen(raw));
    free(raw);
    return MS_OBJ_VAL(s);
}

/* ================================================================
 * debug.is_native(v) -> bool
 * ================================================================ */

static MsValue ms_debug_is_native(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(MS_IS_OBJ_TYPE(argv[0], MS_OBJ_NATIVE));
}

/* ================================================================
 * debug.is_closure(v) -> bool
 * ================================================================ */

static MsValue ms_debug_is_closure(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(MS_IS_OBJ_TYPE(argv[0], MS_OBJ_CLOSURE));
}

/* ================================================================
 * debug.typeof(v) -> str  (same as global type())
 * ================================================================ */

static MsValue ms_debug_typeof(MsVM* vm, int argc, MsValue* argv) {
    const char* t = "nil";
    if (argc < 1) goto done;
    MsValue val = argv[0];
    if      (MS_IS_BOOL(val))    { t = "bool"; }
    else if (MS_IS_NUMBER(val))  { t = "number"; }
    else if (MS_IS_INT(val))     { t = "int"; }
    else if (MS_IS_OBJECT(val)) {
        switch (MS_OBJ_TYPE(val)) {
        case MS_OBJ_STRING:       t = "string";   break;
        case MS_OBJ_FUNCTION:
        case MS_OBJ_CLOSURE:
        case MS_OBJ_NATIVE:       t = "function"; break;
        case MS_OBJ_CLASS:        t = "class";    break;
        case MS_OBJ_INSTANCE:     t = "instance"; break;
        case MS_OBJ_LIST:         t = "list";     break;
        case MS_OBJ_MAP:          t = "map";      break;
        case MS_OBJ_TUPLE:        t = "tuple";    break;
        default:                  t = "object";   break;
        }
    }
done:
    return MS_OBJ_VAL(ms_obj_string_copy(vm, t, (int)strlen(t)));
}

/* ================================================================
 * debug.vm_stats() -> map
 * ================================================================ */

static MsValue ms_debug_vm_stats(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(argc); MS_UNUSED(argv);
    MsObjMap* map = ms_obj_map_new(vm);
    int stack_used = (int)(vm->ctx->stack_top - vm->ctx->stack);
    map_set_str(vm, map, "stack_used",  MS_INT_VAL((ms_i64)stack_used));
    map_set_str(vm, map, "frame_used",  MS_INT_VAL((ms_i64)vm->ctx->frame_count));
    map_set_str(vm, map, "gc_bytes",    MS_INT_VAL((ms_i64)vm->bytes_allocated));
    map_set_str(vm, map, "gc_threshold",MS_INT_VAL((ms_i64)vm->next_gc));
    return MS_OBJ_VAL(map);
}

/* ================================================================
 * debug.gc_trace(enable) -> nil
 * ================================================================ */

static bool g_gc_trace = false;

static MsValue ms_debug_gc_trace(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm);
    if (argc < 1 || !MS_IS_BOOL(argv[0])) {
        ms_vm_runtime_error(vm, "debug.gc_trace: expected bool");
        return MS_NIL_VAL();
    }
    g_gc_trace = MS_AS_BOOL(argv[0]);
    return MS_NIL_VAL();
}

/* ================================================================
 * Registration
 * ================================================================ */

static const MsNativeDef ms_debug_defs[] = {
    {"traceback",  ms_debug_traceback,  -1},
    {"frame_info", ms_debug_frame_info, -1},
    {"locals",     ms_debug_locals,     -1},
    {"upvalues",   ms_debug_upvalues,    1},
    {"disasm",     ms_debug_disasm,      1},
    {"is_native",  ms_debug_is_native,   1},
    {"is_closure", ms_debug_is_closure,  1},
    {"typeof",     ms_debug_typeof,      1},
    {"vm_stats",   ms_debug_vm_stats,    0},
    {"gc_trace",   ms_debug_gc_trace,    1},
    {NULL, NULL, 0}
};

void ms_module_debug_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_debug_defs);
}
