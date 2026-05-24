/* src/stdlib/errors.c - STDLIB-17: errors module
 *
 * errors.new(msg)          → Error instance
 * errors.wrap(cause, msg)  → Error instance with cause chain
 * errors.unwrap(err)       → err.cause | nil
 * errors.message(err)      → err.message (str)
 * errors.is(err, target)   → bool  (chain search by identity)
 * errors.chain(err)        → list  (full cause chain)
 * errors.format(err)       → str   (colon-joined chain messages)
 *
 * Error instances are plain MsObjInstance objects belonging to the
 * errors.Error class exposed by this module.  Fields: message (str),
 * cause (Error | nil).
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/table.h"
#include "ms/shape.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Module-level Error class (stored as a module export "Error")        */
/* ------------------------------------------------------------------ */

/* Intern a short C string into the VM string pool. */
static MsObjString* intern(MsVM* vm, const char* s) {
    return ms_obj_string_copy(vm, s, (int)strlen(s));
}

/* Get the Error class exported from the errors module.
   We store it in the module's exports table under "Error". */
static MsObjClass* get_error_class(MsVM* vm, MsObjModule* mod) {
    MsObjString* key = intern(vm, "Error");
    MsValue v;
    if (ms_table_get(&mod->exports, key, &v) && MS_IS_CLASS(v))
        return MS_AS_CLASS(v);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Instance field helpers                                              */
/* ------------------------------------------------------------------ */

static void inst_set(MsVM* vm, MsObjInstance* inst,
                     const char* name, MsValue val) {
    MsObjString* key = intern(vm, name);
    int slot = ms_shape_find_slot(inst->shape, key);
    if (slot < 0) {
        inst->shape = ms_shape_transition(vm, inst->shape, key);
        slot = ms_shape_find_slot(inst->shape, key);
    }
    if (slot >= MS_SBO_FIELDS && !inst->overflow_fields) {
        int extra = slot - MS_SBO_FIELDS + 1;
        inst->overflow_fields = (MsValue*)calloc((size_t)extra, sizeof(MsValue));
    }
    *ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot) = val;
}

static MsValue inst_get(MsObjInstance* inst, const char* name) {
    /* Linear scan via shape — O(n) but fields are tiny (2 fields). */
    for (int i = 0; i < inst->shape->slots.count; i++) {
        MsSmallEntry* e = &inst->shape->slots.data[i];
        if (e->key && strcmp(e->key->data, name) == 0) {
            return *ms_shape_field_ptr(inst->inline_fields,
                                       inst->overflow_fields,
                                       (int)e->value);
        }
    }
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* Type guard: check that v is an Error instance                       */
/* ------------------------------------------------------------------ */

static bool is_error_inst(MsValue v) {
    if (!MS_IS_INSTANCE(v)) return false;
    MsObjInstance* inst = MS_AS_INSTANCE(v);
    return strcmp(inst->klass->name->data, "Error") == 0;
}

/* ------------------------------------------------------------------ */
/* make_error: allocate a new Error instance with message + cause      */
/* ------------------------------------------------------------------ */

static MsValue make_error(MsVM* vm, MsObjModule* mod,
                           MsValue msg_val, MsValue cause_val) {
    MsObjClass* klass = get_error_class(vm, mod);
    if (!klass) {
        ms_vm_runtime_error(vm, "errors: internal: Error class missing");
        return MS_NIL_VAL();
    }
    MsObjInstance* inst = ms_obj_instance_new(vm, klass);
    inst_set(vm, inst, "message", msg_val);
    inst_set(vm, inst, "cause",   cause_val);
    return MS_OBJ_VAL(inst);
}

/* ------------------------------------------------------------------ */
/* Native implementations                                              */
/* ------------------------------------------------------------------ */

/* Look up the errors module from the module cache.
   Builtin modules are cached under the synthetic key "<builtin:NAME>". */
static MsObjModule* get_errors_module(MsVM* vm) {
    const char* cache_key = "<builtin:errors>";
    MsObjString* key = intern(vm, cache_key);
    MsValue mod_val;
    if (ms_table_get(&vm->module_cache, key, &mod_val) && MS_IS_MODULE(mod_val))
        return MS_AS_MODULE(mod_val);
    return NULL;
}

static MsValue native_new(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "errors.new: expected string message");
        return MS_NIL_VAL();
    }
    MsObjModule* mod = get_errors_module(vm);
    if (!mod) {
        ms_vm_runtime_error(vm, "errors.new: errors module not found in cache");
        return MS_NIL_VAL();
    }
    return make_error(vm, mod, argv[0], MS_NIL_VAL());
}

static MsValue native_wrap(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2) {
        ms_vm_runtime_error(vm, "errors.wrap: expected (cause, msg)");
        return MS_NIL_VAL();
    }
    if (!is_error_inst(argv[0])) {
        ms_vm_runtime_error(vm, "errors.wrap: first argument must be an Error");
        return MS_NIL_VAL();
    }
    if (!MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "errors.wrap: second argument must be string message");
        return MS_NIL_VAL();
    }
    MsObjModule* mod = get_errors_module(vm);
    if (!mod) {
        ms_vm_runtime_error(vm, "errors.wrap: errors module not found in cache");
        return MS_NIL_VAL();
    }
    return make_error(vm, mod, argv[1], argv[0]);
}

static MsValue native_unwrap(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !is_error_inst(argv[0])) {
        ms_vm_runtime_error(vm, "errors.unwrap: expected Error");
        return MS_NIL_VAL();
    }
    return inst_get(MS_AS_INSTANCE(argv[0]), "cause");
}

static MsValue native_message(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !is_error_inst(argv[0])) {
        ms_vm_runtime_error(vm, "errors.message: expected Error");
        return MS_NIL_VAL();
    }
    return inst_get(MS_AS_INSTANCE(argv[0]), "message");
}

static MsValue native_is(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !is_error_inst(argv[0]) || !is_error_inst(argv[1])) {
        ms_vm_runtime_error(vm, "errors.is: expected (Error, Error)");
        return MS_NIL_VAL();
    }
    MsValue cur = argv[0];
    MsValue target = argv[1];
    /* Walk the cause chain; compare by object pointer identity */
    int limit = 256;
    while (is_error_inst(cur) && limit-- > 0) {
        if (MS_AS_OBJECT(cur) == MS_AS_OBJECT(target))
            return MS_BOOL_VAL(true);
        cur = inst_get(MS_AS_INSTANCE(cur), "cause");
    }
    return MS_BOOL_VAL(false);
}

static MsValue native_chain(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !is_error_inst(argv[0])) {
        ms_vm_runtime_error(vm, "errors.chain: expected Error");
        return MS_NIL_VAL();
    }
    MsObjList* lst = ms_obj_list_new(vm);
    MsValue cur = argv[0];
    int limit = 256;
    while (is_error_inst(cur) && limit-- > 0) {
        ms_value_array_push(&lst->items, cur);
        cur = inst_get(MS_AS_INSTANCE(cur), "cause");
    }
    return MS_OBJ_VAL(lst);
}

static MsValue native_format(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !is_error_inst(argv[0])) {
        ms_vm_runtime_error(vm, "errors.format: expected Error");
        return MS_NIL_VAL();
    }
    /* Collect messages: "outer: inner: root" */
    char buf[4096];
    int pos = 0;
    MsValue cur = argv[0];
    int limit = 256;
    bool first = true;
    while (is_error_inst(cur) && limit-- > 0) {
        MsValue msg = inst_get(MS_AS_INSTANCE(cur), "message");
        const char* s = MS_IS_STRING(msg) ? MS_AS_CSTRING(msg) : "(no message)";
        int slen = (int)strlen(s);
        if (!first) {
            if (pos + 2 < (int)sizeof(buf) - 1) {
                buf[pos++] = ':';
                buf[pos++] = ' ';
            }
        }
        if (pos + slen < (int)sizeof(buf) - 1) {
            memcpy(buf + pos, s, (size_t)slen);
            pos += slen;
        }
        first = false;
        cur = inst_get(MS_AS_INSTANCE(cur), "cause");
    }
    buf[pos] = '\0';
    MsObjString* result = ms_obj_string_copy(vm, buf, pos);
    return MS_OBJ_VAL(result);
}

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

void ms_module_errors_init(MsVM* vm, MsObjModule* mod) {
    /* Create and export the Error class */
    MsObjString* class_name = intern(vm, "Error");
    MsObjClass*  klass      = ms_obj_class_new(vm, class_name);
    MsValue      klass_val  = MS_OBJ_VAL(klass);
    ms_module_export_value(vm, mod, "Error", klass_val);

    static const MsNativeDef defs[] = {
        { "new",     native_new,     1  },
        { "wrap",    native_wrap,    2  },
        { "unwrap",  native_unwrap,  1  },
        { "message", native_message, 1  },
        { "is",      native_is,      2  },
        { "chain",   native_chain,   1  },
        { "format",  native_format,  1  },
        { NULL, NULL, 0 }
    };
    ms_module_register_natives(vm, mod, defs);
}
