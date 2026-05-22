/* src/stdlib/gc.c - STDLIB-10: gc module
 * Manual GC control, statistics, pause/resume.
 */
#include "ms/stdlib_register.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/memory.h"
#include "ms/object.h"
#include "ms/vtable.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Trigger & stats                                                       */
/* ------------------------------------------------------------------ */

static MsValue gc_native_collect(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    int before = vm->alive_count;
    ms_gc_collect(vm);
    int freed = before - vm->alive_count;
    return MS_INT_VAL(freed < 0 ? 0 : freed);
}

static MsValue gc_native_minor(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    int before = vm->alive_count;
    ms_gc_collect_minor(vm);
    int freed = before - vm->alive_count;
    return MS_INT_VAL(freed < 0 ? 0 : freed);
}

static MsValue ms_gc_alive(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    return MS_INT_VAL(vm->alive_count);
}

static MsValue ms_gc_bytes_allocated(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    return MS_INT_VAL((ms_i64)vm->bytes_allocated);
}

static MsValue ms_gc_threshold(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    return MS_INT_VAL((ms_i64)vm->next_gc);
}

/* gc.object_counts() → map{type_name: count} */
static MsValue ms_gc_object_counts(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;

    /* 20 object types in the MsObjectType enum */
    static const int kTypeCount = 20;
    static const char* kNames[] = {
        "string", "function", "native", "closure", "upvalue",
        "class", "instance", "bound_method", "list", "map",
        "module", "string_builder", "tuple", "file", "buffer",
        "weak_ref", "coroutine", "future", "socket", "userdata",
    };

    int counts[20] = {0};

    /* Walk all three object lists */
    MsObject* o = vm->objects;
    while (o) { counts[o->type]++; o = o->next; }
    o = vm->young_objects;
    while (o) { counts[o->type]++; o = o->next; }
    o = vm->old_objects;
    while (o) { counts[o->type]++; o = o->next; }

    MsValue map = MS_OBJ_VAL(ms_obj_map_new(vm));
    MsValueTable* tbl = &MS_AS_MAP(map)->table;

    for (int i = 0; i < kTypeCount; i++) {
        if (counts[i] == 0) continue;
        MsValue k = MS_OBJ_VAL(ms_obj_string_copy(vm, kNames[i], (int)strlen(kNames[i])));
        ms_vtable_set(tbl, k, MS_INT_VAL(counts[i]));
    }
    return map;
}

/* ------------------------------------------------------------------ */
/* Control                                                              */
/* ------------------------------------------------------------------ */

static MsValue ms_gc_set_threshold(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_INT(argv[0])) {
        ms_vm_runtime_error(vm, "gc.set_threshold: expected int");
        return MS_NIL_VAL();
    }
    int64_t n = MS_AS_INT(argv[0]);
    if (n < 1024) n = 1024;
    vm->next_gc = (size_t)n;
    return MS_NIL_VAL();
}

static MsValue ms_gc_pause(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    vm->gc_paused = true;
    return MS_NIL_VAL();
}

static MsValue ms_gc_resume(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    vm->gc_paused = false;
    return MS_NIL_VAL();
}

static MsValue ms_gc_is_paused(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    return MS_BOOL_VAL(vm->gc_paused);
}

/* ------------------------------------------------------------------ */
/* Registration                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_gc_defs[] = {
    {"collect",          gc_native_collect,      0},
    {"minor",            gc_native_minor,        0},
    {"alive",            ms_gc_alive,            0},
    {"bytes_allocated",  ms_gc_bytes_allocated,  0},
    {"threshold",        ms_gc_threshold,        0},
    {"object_counts",    ms_gc_object_counts,    0},
    {"set_threshold",    ms_gc_set_threshold,    1},
    {"pause",            ms_gc_pause,            0},
    {"resume",           ms_gc_resume,           0},
    {"is_paused",        ms_gc_is_paused,        0},
    {NULL, NULL, 0}
};

void ms_module_gc_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_gc_defs);
}
