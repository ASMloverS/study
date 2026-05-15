#include "ms/object.h"
#include "ms/table.h"
#include "ms/vtable.h"
#include "ms/vm.h"
#include "ms/memory.h"
#include "ms/consts.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MsObject* ms_alloc_object(struct MsVM* vm, size_t size, MsObjectType type) {
    /* Trigger minor GC before allocation so the new object doesn't exist during sweep */
    if (vm->young_bytes > MS_GC_NURSERY_SIZE)
        ms_gc_collect_minor(vm);
    MsObject* obj = (MsObject*)ms_reallocate(vm, NULL, 0, size);
    obj->type = type;
    obj->is_marked = false;
    obj->in_remembered_set = false;
    obj->generation = 0;
    obj->age = 0;
    obj->next = vm->young_objects;
    vm->young_objects = obj;
    vm->young_bytes += size;
    return obj;
}

uint32_t ms_fnv1a(const char* data, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

static MsObjString* alloc_string(struct MsVM* vm, const char* chars, int length,
                                  uint32_t hash) {
    MsObjString* s = MS_ALLOC_OBJ(vm, MS_OBJ_STRING, MsObjString,
                                   (size_t)length + 1);
    s->length = length;
    s->hash = hash;
    memcpy(s->data, chars, (size_t)length);
    s->data[length] = '\0';
    ms_table_set(&vm->strings, s, MS_BOOL_VAL(true));
    return s;
}

MsObjString* ms_obj_string_copy(struct MsVM* vm, const char* chars, int length) {
    if (length == 1 && (unsigned char)chars[0] < 128 && vm->ascii_cache[(unsigned char)chars[0]])
        return vm->ascii_cache[(unsigned char)chars[0]];
    uint32_t hash = ms_fnv1a(chars, length);
    MsObjString* interned = ms_table_find_string(&vm->strings, chars, length, hash);
    if (interned) return interned;
    return alloc_string(vm, chars, length, hash);
}

MsObjString* ms_obj_string_take(struct MsVM* vm, char* chars, int length) {
    uint32_t hash = ms_fnv1a(chars, length);
    MsObjString* interned = ms_table_find_string(&vm->strings, chars, length, hash);
    if (interned) {
        free(chars);
        return interned;
    }
    MsObjString* s = alloc_string(vm, chars, length, hash);
    free(chars);
    return s;
}

MsObjString* ms_obj_string_concat(struct MsVM* vm, MsObjString* a, MsObjString* b) {
    if (a->length > INT_MAX - b->length) abort();
    int length = a->length + b->length;
    char* buf = (char*)malloc((size_t)length + 1);
    if (!buf) abort();
    memcpy(buf, a->data, (size_t)a->length);
    memcpy(buf + a->length, b->data, (size_t)b->length);
    buf[length] = '\0';

    uint32_t hash = ms_fnv1a(buf, length);
    MsObjString* interned = ms_table_find_string(&vm->strings, buf, length, hash);
    if (interned) {
        free(buf);
        return interned;
    }
    MsObjString* s = alloc_string(vm, buf, length, hash);
    free(buf);
    return s;
}

MsObjFunction* ms_obj_function_new(struct MsVM* vm) {
    MsObjFunction* fn = MS_ALLOC_OBJ(vm, MS_OBJ_FUNCTION, MsObjFunction, 0);
    fn->arity = 0;
    fn->min_arity = -1;
    fn->upvalue_count = 0;
    fn->max_stack_size = 0;
    fn->is_generator = false;
    fn->name = NULL;
    fn->script_path = NULL;
    fn->ic = NULL;
    fn->ic_count = 0;
    fn->arith_deopt = NULL;
    fn->arith_deopt_size = 0;
    ms_chunk_init(&fn->chunk);
    return fn;
}

MsObjNative* ms_obj_native_new(struct MsVM* vm, MsNativeFn fn,
                                const char* name, int arity) {
    MsObjNative* n = MS_ALLOC_OBJ(vm, MS_OBJ_NATIVE, MsObjNative, 0);
    n->function = fn;
    n->name = ms_obj_string_copy(vm, name, (int)strlen(name));
    n->arity = arity;
    return n;
}

MsObjUpvalue* ms_obj_upvalue_new(struct MsVM* vm, MsValue* slot) {
    MsObjUpvalue* uv = (MsObjUpvalue*)ms_pool_alloc(&vm->upvalue_pool);
    uv->obj.type              = MS_OBJ_UPVALUE;
    uv->obj.is_marked         = false;
    uv->obj.in_remembered_set = false;
    uv->obj.generation        = 0;
    uv->obj.age               = 0;
    uv->obj.next              = vm->young_objects;
    vm->young_objects         = (MsObject*)uv;
    uv->location = slot;
    uv->closed   = MS_NIL_VAL();
    uv->next     = NULL;
    return uv;
}

MsObjClosure* ms_obj_closure_new(struct MsVM* vm, MsObjFunction* fn) {
    size_t extra = sizeof(MsObjUpvalue*) * (size_t)fn->upvalue_count;
    MsObjClosure* cl = MS_ALLOC_OBJ(vm, MS_OBJ_CLOSURE, MsObjClosure, extra);
    cl->function = fn;
    cl->upvalue_count = fn->upvalue_count;
    memset(cl->upvalues, 0, extra);
    return cl;
}

MsObjClass* ms_obj_class_new(struct MsVM* vm, MsObjString* name) {
    MsObjClass* klass = MS_ALLOC_OBJ(vm, MS_OBJ_CLASS, MsObjClass, 0);
    klass->name = name;
    ms_table_init(&klass->methods);
    klass->static_methods   = NULL;
    klass->getters          = NULL;
    klass->setters          = NULL;
    klass->abstract_methods = NULL;
    klass->superclass       = NULL;
    klass->root_shape       = ms_shape_new(vm);
    return klass;
}

MsObjInstance* ms_obj_instance_new(struct MsVM* vm, MsObjClass* klass) {
    MsObjInstance* inst = MS_ALLOC_OBJ(vm, MS_OBJ_INSTANCE, MsObjInstance, 0);
    inst->klass           = klass;
    inst->shape           = klass->root_shape;
    inst->overflow_fields = NULL;
    inst->field_count     = 0;
    /* Initialise inline fields to nil */
    for (int i = 0; i < MS_SBO_FIELDS; i++)
        inst->inline_fields[i] = MS_NIL_VAL();
    return inst;
}

MsObjBoundMethod* ms_obj_bound_method_new(struct MsVM* vm, MsValue receiver,
                                           MsObjClosure* method) {
    MsObjBoundMethod* bm = (MsObjBoundMethod*)ms_pool_alloc(&vm->bound_pool);
    bm->obj.type              = MS_OBJ_BOUND_METHOD;
    bm->obj.is_marked         = false;
    bm->obj.in_remembered_set = false;
    bm->obj.generation        = 0;
    bm->obj.age               = 0;
    bm->obj.next              = vm->young_objects;
    vm->young_objects         = (MsObject*)bm;
    bm->receiver = receiver;
    bm->method   = method;
    return bm;
}

MsObjList* ms_obj_list_new(struct MsVM* vm) {
    MsObjList* list = MS_ALLOC_OBJ(vm, MS_OBJ_LIST, MsObjList, 0);
    ms_value_array_init(&list->items);
    return list;
}

MsObjMap* ms_obj_map_new(struct MsVM* vm) {
    MsObjMap* map = MS_ALLOC_OBJ(vm, MS_OBJ_MAP, MsObjMap, 0);
    ms_vtable_init(&map->table);
    return map;
}

MsObjTuple* ms_obj_tuple_new(struct MsVM* vm, MsValue* items, int count) {
    size_t extra = sizeof(MsValue) * (size_t)count;
    MsObjTuple* t = MS_ALLOC_OBJ(vm, MS_OBJ_TUPLE, MsObjTuple, extra);
    t->count = count;
    /* compute hash by folding item hashes */
    uint32_t h = 2166136261u;
    for (int i = 0; i < count; i++) {
        t->items[i] = items[i];
        /* mix in raw bits (works for both tagged-union and NaN-boxing) */
#if MS_NAN_BOXING
        h ^= (uint32_t)(items[i] >> 48); /* top 16 bits encode the tag */
#else
        h ^= (uint32_t)items[i].type;
#endif
        h *= 16777619u;
        if (MS_IS_INT(items[i]))    h ^= (uint32_t)(ms_u64)MS_AS_INT(items[i]);
        else if (MS_IS_NUMBER(items[i])) { union { double d; uint64_t u; } cv;
            cv.d = MS_AS_NUMBER(items[i]); h ^= (uint32_t)cv.u; }
        else if (MS_IS_STRING(items[i])) h ^= MS_AS_STRING(items[i])->hash;
        h *= 16777619u;
    }
    t->hash = h;
    return t;
}

static void print_fn_name(MsObjFunction* fn) {
    if (fn->name) printf("<fn %s>", fn->name->data);
    else          printf("<fn>");
}

void ms_object_print(MsObject* obj) {
    switch (obj->type) {
        case MS_OBJ_STRING:
            printf("%s", ((MsObjString*)obj)->data);
            break;
        case MS_OBJ_FUNCTION:
            print_fn_name((MsObjFunction*)obj);
            break;
        case MS_OBJ_NATIVE: {
            MsObjNative* n = (MsObjNative*)obj;
            if (n->name) printf("<native %s>", n->name->data);
            else         printf("<native>");
            break;
        }
        case MS_OBJ_CLOSURE:
            print_fn_name(((MsObjClosure*)obj)->function);
            break;
        case MS_OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case MS_OBJ_CLASS: {
            MsObjClass* klass = (MsObjClass*)obj;
            if (klass->name) printf("<class %s>", klass->name->data);
            else             printf("<class>");
            break;
        }
        case MS_OBJ_INSTANCE: {
            MsObjInstance* inst = (MsObjInstance*)obj;
            if (inst->klass && inst->klass->name)
                printf("<%s instance>", inst->klass->name->data);
            else
                printf("<instance>");
            break;
        }
        case MS_OBJ_BOUND_METHOD: {
            MsObjBoundMethod* bm = (MsObjBoundMethod*)obj;
            print_fn_name(bm->method->function);
            break;
        }
        case MS_OBJ_LIST: {
            MsObjList* list = (MsObjList*)obj;
            printf("[");
            for (int i = 0; i < list->items.count; i++) {
                if (i > 0) printf(", ");
                ms_value_print(list->items.data[i]);
            }
            printf("]");
            break;
        }
        case MS_OBJ_MAP: {
            printf("<map>");
            break;
        }
        case MS_OBJ_TUPLE: {
            MsObjTuple* tup = (MsObjTuple*)obj;
            printf("(");
            for (int i = 0; i < tup->count; i++) {
                if (i > 0) printf(", ");
                ms_value_print(tup->items[i]);
            }
            printf(")");
            break;
        }
        case MS_OBJ_MODULE: {
            MsObjModule* mod = (MsObjModule*)obj;
            if (mod->name) printf("<module %s>", mod->name->data);
            else           printf("<module>");
            break;
        }
        case MS_OBJ_STRING_BUILDER: {
            MsObjStringBuilder* sb = (MsObjStringBuilder*)obj;
            printf("<StringBuilder len=%d>", sb->length);
            break;
        }
        case MS_OBJ_COROUTINE: {
            MsObjCoroutine* co = (MsObjCoroutine*)obj;
            const char* state = "?";
            switch (co->state) {
            case MS_CORO_CREATED:   state = "created";   break;
            case MS_CORO_RUNNING:   state = "running";   break;
            case MS_CORO_SUSPENDED: state = "suspended"; break;
            case MS_CORO_DEAD:      state = "dead";      break;
            }
            printf("<coroutine %s>", state);
            break;
        }
        default:
            printf("<object %d>", (int)obj->type);
            break;
    }
}

void ms_object_free(struct MsVM* vm, MsObject* obj) {
    switch (obj->type) {
        case MS_OBJ_STRING: {
            MsObjString* s = (MsObjString*)obj;
            ms_reallocate(vm, obj, sizeof(MsObjString) + (size_t)s->length + 1, 0);
            break;
        }
        case MS_OBJ_FUNCTION: {
            MsObjFunction* fn = (MsObjFunction*)obj;
            ms_chunk_free(&fn->chunk);
            if (fn->arith_deopt) free(fn->arith_deopt);
            ms_reallocate(vm, obj, sizeof(MsObjFunction), 0);
            break;
        }
        case MS_OBJ_NATIVE:
            ms_reallocate(vm, obj, sizeof(MsObjNative), 0);
            break;
        case MS_OBJ_UPVALUE:
            ms_pool_free_obj(&vm->upvalue_pool, obj);
            break;
        case MS_OBJ_CLOSURE: {
            MsObjClosure* cl = (MsObjClosure*)obj;
            size_t sz = sizeof(MsObjClosure) +
                        sizeof(MsObjUpvalue*) * (size_t)cl->upvalue_count;
            ms_reallocate(vm, obj, sz, 0);
            break;
        }
        case MS_OBJ_CLASS: {
            MsObjClass* klass = (MsObjClass*)obj;
            ms_table_free(&klass->methods);
            if (klass->static_methods)   { ms_table_free(klass->static_methods);   free(klass->static_methods); }
            if (klass->getters)          { ms_table_free(klass->getters);           free(klass->getters); }
            if (klass->setters)          { ms_table_free(klass->setters);           free(klass->setters); }
            if (klass->abstract_methods) { ms_table_free(klass->abstract_methods);  free(klass->abstract_methods); }
            ms_reallocate(vm, obj, sizeof(MsObjClass), 0);
            break;
        }
        case MS_OBJ_INSTANCE: {
            MsObjInstance* inst = (MsObjInstance*)obj;
            if (inst->overflow_fields) {
                int overflow_count = inst->field_count - MS_SBO_FIELDS;
                if (overflow_count > 0)
                    MS_FREE_ARRAY(vm, MsValue, inst->overflow_fields,
                                  overflow_count);
                inst->overflow_fields = NULL;
            }
            ms_reallocate(vm, obj, sizeof(MsObjInstance), 0);
            break;
        }
        case MS_OBJ_BOUND_METHOD:
            ms_pool_free_obj(&vm->bound_pool, obj);
            break;
        case MS_OBJ_LIST: {
            MsObjList* list = (MsObjList*)obj;
            ms_value_array_free(&list->items);
            ms_reallocate(vm, obj, sizeof(MsObjList), 0);
            break;
        }
        case MS_OBJ_MAP: {
            MsObjMap* map = (MsObjMap*)obj;
            ms_vtable_free(&map->table);
            ms_reallocate(vm, obj, sizeof(MsObjMap), 0);
            break;
        }
        case MS_OBJ_TUPLE: {
            MsObjTuple* tup = (MsObjTuple*)obj;
            size_t sz = sizeof(MsObjTuple) + sizeof(MsValue) * (size_t)tup->count;
            ms_reallocate(vm, obj, sz, 0);
            break;
        }
        case MS_OBJ_MODULE: {
            MsObjModule* mod = (MsObjModule*)obj;
            ms_table_free(&mod->exports);
            ms_reallocate(vm, obj, sizeof(MsObjModule), 0);
            break;
        }
        case MS_OBJ_STRING_BUILDER: {
            MsObjStringBuilder* sb = (MsObjStringBuilder*)obj;
            if (sb->buffer) ms_reallocate(vm, sb->buffer, (size_t)sb->capacity, 0);
            ms_reallocate(vm, obj, sizeof(MsObjStringBuilder), 0);
            break;
        }
        case MS_OBJ_COROUTINE: {
            MsObjCoroutine* co = (MsObjCoroutine*)obj;
            if (co->stack_buf)
                free(co->stack_buf);
            if (co->ctx.frames)
                free(co->ctx.frames);
            ms_reallocate(vm, obj, sizeof(MsObjCoroutine), 0);
            break;
        }
        default:
            /* Unhandled object type: abort to catch missing free cases early. */
            abort();
            break;
    }
}

MsObjModule* ms_obj_module_new(struct MsVM* vm, MsObjString* name,
                                MsObjString* path) {
    MsObjModule* mod = MS_ALLOC_OBJ(vm, MS_OBJ_MODULE, MsObjModule, 0);
    mod->name  = name;
    mod->path  = path;
    mod->state = MS_MOD_UNSEEN;
    ms_table_init(&mod->exports);
    return mod;
}

MsObjStringBuilder* ms_obj_sb_new(struct MsVM* vm) {
    MsObjStringBuilder* sb = MS_ALLOC_OBJ(vm, MS_OBJ_STRING_BUILDER,
                                            MsObjStringBuilder, 0);
    sb->buffer   = NULL;
    sb->length   = 0;
    sb->capacity = 0;
    return sb;
}

void ms_obj_sb_append(MsObjStringBuilder* sb, const char* str, int len) {
    int needed = sb->length + len + 1;
    if (needed > sb->capacity) {
        int cap = sb->capacity < 16 ? 16 : sb->capacity * 2;
        if (cap < needed) cap = needed;
        sb->buffer = (char*)realloc(sb->buffer, (size_t)cap);
        if (!sb->buffer) abort();
        sb->capacity = cap;
    }
    memcpy(sb->buffer + sb->length, str, (size_t)len);
    sb->length += len;
    sb->buffer[sb->length] = '\0';
}

MsObjString* ms_obj_sb_to_string(struct MsVM* vm, MsObjStringBuilder* sb) {
    return ms_obj_string_copy(vm, sb->buffer ? sb->buffer : "", sb->length);
}

/* ---- Coroutine ---- */

#define CORO_STACK_SIZE 256
#define CORO_FRAME_CAP  16

MsObjCoroutine* ms_obj_coroutine_new(struct MsVM* vm, MsObjClosure* cl) {
    MsObjCoroutine* co = MS_ALLOC_OBJ(vm, MS_OBJ_COROUTINE, MsObjCoroutine, 0);
    co->state       = MS_CORO_CREATED;
    co->closure     = cl;
    co->yield_value = MS_NIL_VAL();

    /* Allocate independent stack buffer */
    co->stack_buf = (MsValue*)malloc(sizeof(MsValue) * (size_t)CORO_STACK_SIZE);
    if (!co->stack_buf) abort();

    /* Allocate independent frames buffer */
    struct MsCallFrame* frames_buf =
        (struct MsCallFrame*)malloc(sizeof(struct MsCallFrame) * (size_t)CORO_FRAME_CAP);
    if (!frames_buf) abort();

    /* Set up execution context */
    co->ctx.stack          = co->stack_buf;
    co->ctx.stack_top      = co->stack_buf;
    co->ctx.frames         = frames_buf;
    co->ctx.frame_count    = 0;
    co->ctx.frame_capacity = CORO_FRAME_CAP;
    co->ctx.stack_capacity = CORO_STACK_SIZE;
    co->ctx.open_upvalues  = NULL;
    return co;
}
