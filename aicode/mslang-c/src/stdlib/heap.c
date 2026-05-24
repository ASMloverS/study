/* src/stdlib/heap.c - STDLIB-22: heap module
 *
 * Priority queue (binary heap) backed by ObjList.
 * Default: min-heap (natural order). Custom cmp(a,b)->int supported.
 *
 * Public API:
 *   heap.new(cmp=nil)             → Heap  (empty, O(1))
 *   heap.heapify(list, cmp=nil)   → Heap  (O(n))
 *   heap.nsmallest(n, list)       → list  (n smallest, ascending)
 *   heap.nlargest(n, list)        → list  (n largest, descending)
 *
 * Instance methods (argv[0] = self):
 *   h.push(value)       → nil   O(log n)
 *   h.pop()             → any   O(log n)
 *   h.peek()            → any   O(1)
 *   h.push_pop(value)   → any   O(log n)
 *   h.size()            → int   O(1)
 *   h.is_empty()        → bool  O(1)
 *   h.to_list()         → list  (copy of internal array, heap order)
 *   h.to_sorted_list()  → list  (destructive drain, sorted)
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/table.h"
#include "ms/shape.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Internals                                                            */
/* ------------------------------------------------------------------ */

static MsObjString* intern(MsVM* vm, const char* s) {
    return ms_obj_string_copy(vm, s, (int)strlen(s));
}

/* ---- Shape-based field helpers ---- */

static void inst_set_field(MsVM* vm, MsObjInstance* inst,
                           const char* name, MsValue val) {
    MsObjString* key = intern(vm, name);
    int slot = ms_shape_find_slot(inst->shape, key);
    if (slot < 0) {
        inst->shape = ms_shape_transition(vm, inst->shape, key);
        slot = ms_shape_find_slot(inst->shape, key);
    }
    if (slot >= MS_SBO_FIELDS && !inst->overflow_fields) {
        int extra = slot - MS_SBO_FIELDS + 1;
        inst->overflow_fields =
            (MsValue*)calloc((size_t)extra, sizeof(MsValue));
    }
    *ms_shape_field_ptr(inst->inline_fields, inst->overflow_fields, slot) = val;
}

static MsValue inst_get_field(MsObjInstance* inst, const char* name) {
    for (int i = 0; i < inst->shape->slots.count; i++) {
        MsSmallEntry* e = &inst->shape->slots.data[i];
        if (e->key && strcmp(e->key->data, name) == 0)
            return *ms_shape_field_ptr(inst->inline_fields,
                                       inst->overflow_fields,
                                       (int)e->value);
    }
    return MS_NIL_VAL();
}

/* ---- Heap class guard ---- */

static bool is_heap_inst(MsValue v) {
    return MS_IS_INSTANCE(v) &&
           strcmp(MS_AS_INSTANCE(v)->klass->name->data, "Heap") == 0;
}

/* ---- Get backing list + cmp from Heap instance ---- */

static MsObjList* heap_list(MsValue v) {
    if (!is_heap_inst(v)) return NULL;
    MsValue d = inst_get_field(MS_AS_INSTANCE(v), "_data");
    return MS_IS_LIST(d) ? MS_AS_LIST(d) : NULL;
}

static MsValue heap_cmp(MsValue v) {
    if (!is_heap_inst(v)) return MS_NIL_VAL();
    return inst_get_field(MS_AS_INSTANCE(v), "_cmp");
}

/* ---- Comparator: returns negative if a has higher priority ---- */

static bool is_callable(MsValue v) {
    if (!MS_IS_OBJECT(v)) return false;
    MsObjectType t = MS_OBJ_TYPE(v);
    return t == MS_OBJ_CLOSURE || t == MS_OBJ_NATIVE || t == MS_OBJ_BOUND_METHOD;
}

/* cmp_lt(a, b): true if a should be above b in the heap.
   Uses user cmp(a,b)<0, or natural min-order. */
static bool cmp_lt(MsVM* vm, MsValue cmp_fn, MsValue a, MsValue b) {
    if (is_callable(cmp_fn)) {
        MsValue args[2] = {a, b};
        MsValue result = MS_NIL_VAL();
        if (ms_vm_call_sync(vm, cmp_fn, args, 2, &result) != MS_INTERPRET_OK)
            return false;
        if (MS_IS_NUMERIC(result)) return ms_as_double(result) < 0.0;
        return false;
    }
    /* natural: numeric or string */
    if (MS_IS_NUMERIC(a) && MS_IS_NUMERIC(b))
        return ms_as_double(a) < ms_as_double(b);
    if (MS_IS_STRING(a) && MS_IS_STRING(b))
        return strcmp(MS_AS_CSTRING(a), MS_AS_CSTRING(b)) < 0;
    return false;
}

/* ---- Binary heap sift operations ---- */

static void sift_up(MsVM* vm, MsValueArray* arr, int i, MsValue cmp_fn) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (cmp_lt(vm, cmp_fn, arr->data[i], arr->data[parent])) {
            MsValue tmp = arr->data[i];
            arr->data[i] = arr->data[parent];
            arr->data[parent] = tmp;
            i = parent;
        } else {
            break;
        }
    }
}

static void sift_down(MsVM* vm, MsValueArray* arr, int i, int n, MsValue cmp_fn) {
    while (true) {
        int smallest = i;
        int left  = 2 * i + 1;
        int right = 2 * i + 2;
        if (left  < n && cmp_lt(vm, cmp_fn, arr->data[left],  arr->data[smallest]))
            smallest = left;
        if (right < n && cmp_lt(vm, cmp_fn, arr->data[right], arr->data[smallest]))
            smallest = right;
        if (smallest == i) break;
        MsValue tmp = arr->data[i];
        arr->data[i] = arr->data[smallest];
        arr->data[smallest] = tmp;
        i = smallest;
    }
}

/* ---- Heap class lookup ---- */

static MsObjClass* get_heap_class(MsVM* vm) {
    MsObjString* key = intern(vm, "<builtin:heap>");
    MsValue mod_val;
    if (!ms_table_get(&vm->module_cache, key, &mod_val)) return NULL;
    if (!MS_IS_MODULE(mod_val)) return NULL;
    MsObjModule* mod = MS_AS_MODULE(mod_val);
    MsObjString* ckey = intern(vm, "Heap");
    MsValue cv;
    if (!ms_table_get(&mod->exports, ckey, &cv)) return NULL;
    return MS_IS_CLASS(cv) ? MS_AS_CLASS(cv) : NULL;
}

/* ---- Heap instance constructor ---- */

static MsValue make_heap(MsVM* vm, MsObjClass* klass, MsValue cmp_fn) {
    MsObjInstance* inst = ms_obj_instance_new(vm, klass);
    MsObjList* lst = ms_obj_list_new(vm);
    inst_set_field(vm, inst, "_data", MS_OBJ_VAL(lst));
    inst_set_field(vm, inst, "_cmp",  cmp_fn);
    return MS_OBJ_VAL(inst);
}

/* ------------------------------------------------------------------ */
/* Module-level functions                                              */
/* ------------------------------------------------------------------ */

/* heap.new(cmp=nil) */
static MsValue heap_new(MsVM* vm, int argc, MsValue* argv) {
    MsValue cmp_fn = MS_NIL_VAL();
    if (argc >= 1 && !MS_IS_NIL(argv[0])) {
        if (!is_callable(argv[0])) {
            ms_vm_runtime_error(vm, "heap.new: cmp must be callable or nil");
            return MS_NIL_VAL();
        }
        cmp_fn = argv[0];
    }
    MsObjClass* klass = get_heap_class(vm);
    if (!klass) { ms_vm_runtime_error(vm, "heap.new: Heap class not found"); return MS_NIL_VAL(); }
    return make_heap(vm, klass, cmp_fn);
}

/* heap.heapify(list, cmp=nil) */
static MsValue heap_heapify(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "heap.heapify: first argument must be a list");
        return MS_NIL_VAL();
    }
    MsValue cmp_fn = MS_NIL_VAL();
    if (argc >= 2 && !MS_IS_NIL(argv[1])) {
        if (!is_callable(argv[1])) {
            ms_vm_runtime_error(vm, "heap.heapify: cmp must be callable or nil");
            return MS_NIL_VAL();
        }
        cmp_fn = argv[1];
    }
    MsObjClass* klass = get_heap_class(vm);
    if (!klass) { ms_vm_runtime_error(vm, "heap.heapify: Heap class not found"); return MS_NIL_VAL(); }

    MsObjList* src = MS_AS_LIST(argv[0]);
    MsValue h = make_heap(vm, klass, cmp_fn);
    MsObjList* lst = MS_AS_LIST(inst_get_field(MS_AS_INSTANCE(h), "_data"));

    /* copy elements */
    for (int i = 0; i < src->items.count; i++)
        ms_value_array_push(&lst->items, src->items.data[i]);

    /* Floyd's O(n) build-heap */
    int n = lst->items.count;
    for (int i = n / 2 - 1; i >= 0; i--)
        sift_down(vm, &lst->items, i, n, cmp_fn);

    return h;
}

/* heap.nsmallest(n, list) */
static MsValue heap_nsmallest(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_INT(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "heap.nsmallest: expected (int, list)");
        return MS_NIL_VAL();
    }
    int k = (int)MS_AS_INT(argv[0]);
    MsObjList* src = MS_AS_LIST(argv[1]);
    int n = src->items.count;
    if (k <= 0) return MS_OBJ_VAL(ms_obj_list_new(vm));
    if (k > n) k = n;

    /* Use a max-heap of size k to track k smallest */
    MsValue* tmp = (MsValue*)malloc((size_t)n * sizeof(MsValue));
    if (!tmp) { ms_vm_runtime_error(vm, "heap.nsmallest: OOM"); return MS_NIL_VAL(); }
    for (int i = 0; i < n; i++) tmp[i] = src->items.data[i];

    /* negate comparison: build min-heap then pop k times */
    /* Simpler: just partial sort via copy + sort ascending, take k */
    /* Sort copy ascending (insertion sort ok for typical k) */
    for (int i = 1; i < n; i++) {
        MsValue v = tmp[i];
        int j = i - 1;
        while (j >= 0) {
            bool lt = false;
            if (MS_IS_NUMERIC(v) && MS_IS_NUMERIC(tmp[j]))
                lt = ms_as_double(v) < ms_as_double(tmp[j]);
            else if (MS_IS_STRING(v) && MS_IS_STRING(tmp[j]))
                lt = strcmp(MS_AS_CSTRING(v), MS_AS_CSTRING(tmp[j])) < 0;
            if (!lt) break;
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = v;
    }

    MsObjList* result = ms_obj_list_new(vm);
    for (int i = 0; i < k; i++)
        ms_value_array_push(&result->items, tmp[i]);
    free(tmp);
    return MS_OBJ_VAL(result);
}

/* heap.nlargest(n, list) */
static MsValue heap_nlargest(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_INT(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "heap.nlargest: expected (int, list)");
        return MS_NIL_VAL();
    }
    int k = (int)MS_AS_INT(argv[0]);
    MsObjList* src = MS_AS_LIST(argv[1]);
    int n = src->items.count;
    if (k <= 0) return MS_OBJ_VAL(ms_obj_list_new(vm));
    if (k > n) k = n;

    MsValue* tmp = (MsValue*)malloc((size_t)n * sizeof(MsValue));
    if (!tmp) { ms_vm_runtime_error(vm, "heap.nlargest: OOM"); return MS_NIL_VAL(); }
    for (int i = 0; i < n; i++) tmp[i] = src->items.data[i];

    /* Sort descending */
    for (int i = 1; i < n; i++) {
        MsValue v = tmp[i];
        int j = i - 1;
        while (j >= 0) {
            bool gt = false;
            if (MS_IS_NUMERIC(v) && MS_IS_NUMERIC(tmp[j]))
                gt = ms_as_double(v) > ms_as_double(tmp[j]);
            else if (MS_IS_STRING(v) && MS_IS_STRING(tmp[j]))
                gt = strcmp(MS_AS_CSTRING(v), MS_AS_CSTRING(tmp[j])) > 0;
            if (!gt) break;
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = v;
    }

    MsObjList* result = ms_obj_list_new(vm);
    for (int i = 0; i < k; i++)
        ms_value_array_push(&result->items, tmp[i]);
    free(tmp);
    return MS_OBJ_VAL(result);
}

/* ------------------------------------------------------------------ */
/* Instance methods                                                     */
/* ------------------------------------------------------------------ */

/* h.push(value) */
static MsValue heap_push(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjList* lst = heap_list(argv[0]);
    if (!lst) { ms_vm_runtime_error(vm, "heap.push: receiver is not a Heap"); return MS_NIL_VAL(); }
    MsValue cmp_fn = heap_cmp(argv[0]);
    ms_value_array_push(&lst->items, argv[1]);
    sift_up(vm, &lst->items, lst->items.count - 1, cmp_fn);
    return MS_NIL_VAL();
}

/* h.pop() */
static MsValue heap_pop(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjList* lst = heap_list(argv[0]);
    if (!lst) { ms_vm_runtime_error(vm, "heap.pop: receiver is not a Heap"); return MS_NIL_VAL(); }
    int n = lst->items.count;
    if (n == 0) { ms_vm_runtime_error(vm, "heap.pop: heap is empty"); return MS_NIL_VAL(); }
    MsValue top = lst->items.data[0];
    lst->items.data[0] = lst->items.data[n - 1];
    lst->items.count--;
    if (lst->items.count > 0)
        sift_down(vm, &lst->items, 0, lst->items.count, heap_cmp(argv[0]));
    return top;
}

/* h.peek() */
static MsValue heap_peek(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjList* lst = heap_list(argv[0]);
    if (!lst) { ms_vm_runtime_error(vm, "heap.peek: receiver is not a Heap"); return MS_NIL_VAL(); }
    if (lst->items.count == 0) { ms_vm_runtime_error(vm, "heap.peek: heap is empty"); return MS_NIL_VAL(); }
    return lst->items.data[0];
}

/* h.push_pop(value) — push then pop (more efficient than separate ops) */
static MsValue heap_push_pop(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjList* lst = heap_list(argv[0]);
    if (!lst) { ms_vm_runtime_error(vm, "heap.push_pop: receiver is not a Heap"); return MS_NIL_VAL(); }
    MsValue val = argv[1];
    MsValue cmp_fn = heap_cmp(argv[0]);
    if (lst->items.count == 0 || cmp_lt(vm, cmp_fn, val, lst->items.data[0]))
        return val; /* val is already the top */
    MsValue top = lst->items.data[0];
    lst->items.data[0] = val;
    sift_down(vm, &lst->items, 0, lst->items.count, cmp_fn);
    return top;
}

/* h.size() */
static MsValue heap_size(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjList* lst = heap_list(argv[0]);
    if (!lst) { ms_vm_runtime_error(vm, "heap.size: receiver is not a Heap"); return MS_NIL_VAL(); }
    return MS_INT_VAL((ms_i64)lst->items.count);
}

/* h.is_empty() */
static MsValue heap_is_empty(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjList* lst = heap_list(argv[0]);
    if (!lst) { ms_vm_runtime_error(vm, "heap.is_empty: receiver is not a Heap"); return MS_NIL_VAL(); }
    return MS_BOOL_VAL(lst->items.count == 0);
}

/* h.to_list() — copy of internal array (heap order, not sorted) */
static MsValue heap_to_list(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjList* lst = heap_list(argv[0]);
    if (!lst) { ms_vm_runtime_error(vm, "heap.to_list: receiver is not a Heap"); return MS_NIL_VAL(); }
    MsObjList* result = ms_obj_list_new(vm);
    for (int i = 0; i < lst->items.count; i++)
        ms_value_array_push(&result->items, lst->items.data[i]);
    return MS_OBJ_VAL(result);
}

/* h.to_sorted_list() — drain heap into sorted list (destructive) */
static MsValue heap_to_sorted_list(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjList* lst = heap_list(argv[0]);
    if (!lst) { ms_vm_runtime_error(vm, "heap.to_sorted_list: receiver is not a Heap"); return MS_NIL_VAL(); }
    MsValue cmp_fn = heap_cmp(argv[0]);
    MsObjList* result = ms_obj_list_new(vm);
    while (lst->items.count > 0) {
        MsValue top = lst->items.data[0];
        ms_value_array_push(&result->items, top);
        int n = lst->items.count;
        lst->items.data[0] = lst->items.data[n - 1];
        lst->items.count--;
        if (lst->items.count > 0)
            sift_down(vm, &lst->items, 0, lst->items.count, cmp_fn);
    }
    return MS_OBJ_VAL(result);
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

void ms_module_heap_init(MsVM* vm, MsObjModule* mod) {
    /* 1. Create Heap class and export it */
    MsObjClass* klass = ms_obj_class_new(vm, intern(vm, "Heap"));
    ms_module_export_value(vm, mod, "Heap", MS_OBJ_VAL(klass));

    /* 2. Instance methods */
    static const struct { const char* name; MsNativeFn fn; int arity; } methods[] = {
        {"push",          heap_push,         2},
        {"pop",           heap_pop,          1},
        {"peek",          heap_peek,         1},
        {"push_pop",      heap_push_pop,     2},
        {"size",          heap_size,         1},
        {"is_empty",      heap_is_empty,     1},
        {"to_list",       heap_to_list,      1},
        {"to_sorted_list",heap_to_sorted_list,1},
        {NULL, NULL, 0}
    };
    for (int i = 0; methods[i].name; i++) {
        MsObjString* mname = intern(vm, methods[i].name);
        MsObjNative* nat   = ms_obj_native_new(vm, methods[i].fn,
                                                methods[i].name,
                                                methods[i].arity);
        ms_table_set(&klass->methods, mname, MS_OBJ_VAL(nat));
    }

    /* 3. Module-level functions */
    static const MsNativeDef defs[] = {
        {"new",       heap_new,      -1},
        {"heapify",   heap_heapify,  -1},
        {"nsmallest", heap_nsmallest, 2},
        {"nlargest",  heap_nlargest,  2},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);
}
