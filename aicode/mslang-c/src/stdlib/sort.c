/* src/stdlib/sort.c - STDLIB-20: sort module
 *
 * Sorting and binary-search utilities for lists.
 * Public API:
 *   sort.sort(list, key=nil, reverse=false, cmp=nil)  — in-place unstable sort
 *   sort.stable_sort(list, key=nil, reverse=false, cmp=nil) — in-place stable sort
 *   sort.sorted(list, key=nil, reverse=false, cmp=nil) — sorted copy
 *   sort.is_sorted(list, key=nil)     — ascending check
 *   sort.is_sorted_desc(list, key=nil) — descending check
 *   sort.binary_search(list, value, key=nil) — index or -1
 *   sort.bisect_left(list, value, key=nil)   — left insertion point
 *   sort.bisect_right(list, value, key=nil)  — right insertion point
 *   sort.search_range(list, value, key=nil)  — [lo, hi) range list
 *
 * All implemented in C using ms_vm_call_sync for callbacks.
 * Stable sort uses Timsort-style (insertion sort + bottom-up merge).
 * Unstable sort uses introsort (quicksort + heapsort + insertion sort fallback).
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static bool is_callable(MsValue v) {
    if (!MS_IS_OBJECT(v)) return false;
    MsObjectType t = MS_OBJ_TYPE(v);
    return t == MS_OBJ_CLOSURE || t == MS_OBJ_NATIVE || t == MS_OBJ_BOUND_METHOD;
}

/* call fn(a, b) → result. Returns false on error. */
static bool call2(MsVM* vm, MsValue fn, MsValue a, MsValue b, MsValue* out) {
    MsValue args[2] = {a, b};
    if (ms_vm_call_sync(vm, fn, args, 2, out) != MS_INTERPRET_OK) {
        *out = MS_NIL_VAL();
        return false;
    }
    return true;
}

/* call fn(v) → result. Returns false on error. */
static bool call1(MsVM* vm, MsValue fn, MsValue v, MsValue* out) {
    MsValue arg = v;
    if (ms_vm_call_sync(vm, fn, &arg, 1, out) != MS_INTERPRET_OK) {
        *out = MS_NIL_VAL();
        return false;
    }
    return true;
}

/*
 * Compare two values naturally:
 *   numbers (int/float): numeric order
 *   strings: lexicographic
 *   Returns negative, zero, or positive (like strcmp convention).
 *   Mixed types fall back to numeric comparison.
 */
static int value_cmp_natural(MsValue a, MsValue b) {
    /* both numeric */
    if (MS_IS_NUMERIC(a) && MS_IS_NUMERIC(b)) {
        double da = ms_as_double(a);
        double db = ms_as_double(b);
        return (da > db) - (da < db);
    }
    /* both strings */
    if (MS_IS_STRING(a) && MS_IS_STRING(b)) {
        return strcmp(MS_AS_CSTRING(a), MS_AS_CSTRING(b));
    }
    /* mixed: coerce to double */
    double da = MS_IS_NUMERIC(a) ? ms_as_double(a) : 0.0;
    double db = MS_IS_NUMERIC(b) ? ms_as_double(b) : 0.0;
    return (da > db) - (da < db);
}

/* ------------------------------------------------------------------ */
/* SortEntry — item + pre-computed key + original index               */
/* ------------------------------------------------------------------ */

typedef struct {
    MsValue item;
    MsValue key;
    int     orig; /* original index for stable tie-breaking */
} SortEntry;

/*
 * Compute keys for all entries. key_fn may be nil (identity).
 * Returns false if a callback error occurred.
 */
static bool compute_keys(MsVM* vm, SortEntry* arr, int n, MsValue key_fn) {
    bool has_key = !MS_IS_NIL(key_fn);
    for (int i = 0; i < n; i++) {
        arr[i].orig = i;
        if (has_key) {
            if (!call1(vm, key_fn, arr[i].item, &arr[i].key))
                return false;
        } else {
            arr[i].key = arr[i].item;
        }
    }
    return true;
}

/*
 * Compare two SortEntries using a cmp callback (fn(a,b)→int).
 * Returns -1/0/1. Sets *ok=false on error.
 */
static int entry_cmp_fn(MsVM* vm, MsValue cmp_fn,
                        const SortEntry* a, const SortEntry* b,
                        bool reverse, bool* ok) {
    MsValue r;
    if (!call2(vm, cmp_fn, a->item, b->item, &r)) { *ok = false; return 0; }
    int c = 0;
    if (MS_IS_INT(r))    c = (int)MS_AS_INT(r);
    else if (MS_IS_NUMBER(r)) c = MS_AS_NUMBER(r) < 0 ? -1 : (MS_AS_NUMBER(r) > 0 ? 1 : 0);
    if (reverse) c = -c;
    return c;
}

/*
 * Compare two SortEntries using natural key comparison.
 * For stable sort, equal keys fall back to orig index (preserves order).
 */
static int entry_cmp_key(const SortEntry* a, const SortEntry* b,
                         bool reverse, bool stable) {
    int c = value_cmp_natural(a->key, b->key);
    if (c == 0 && stable) c = a->orig - b->orig;
    if (reverse) c = -c;
    return c;
}

/* ------------------------------------------------------------------ */
/* Insertion sort (stable, O(n^2)) — used for small ranges            */
/* ------------------------------------------------------------------ */

static bool insertion_sort(MsVM* vm, SortEntry* arr, int lo, int hi,
                           MsValue cmp_fn, MsValue key_fn, bool reverse,
                           bool stable) {
    (void)key_fn; /* keys are pre-computed in SortEntry.key */
    bool has_cmp = !MS_IS_NIL(cmp_fn);
    for (int i = lo + 1; i < hi; i++) {
        SortEntry cur = arr[i];
        int j = i - 1;
        while (j >= lo) {
            int c;
            if (has_cmp) {
                bool ok = true;
                c = entry_cmp_fn(vm, cmp_fn, &cur, &arr[j], reverse, &ok);
                if (!ok) return false;
            } else {
                c = entry_cmp_key(&cur, &arr[j], reverse, stable);
            }
            if (c >= 0) break;
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = cur;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Bottom-up merge sort (stable)                                       */
/* ------------------------------------------------------------------ */

static bool merge_arrays(MsVM* vm, SortEntry* arr, SortEntry* tmp,
                         int lo, int mid, int hi,
                         MsValue cmp_fn, MsValue key_fn, bool reverse) {
    (void)key_fn; /* keys are pre-computed in SortEntry.key */
    bool has_cmp = !MS_IS_NIL(cmp_fn);
    int len = hi - lo;
    memcpy(tmp, arr + lo, sizeof(SortEntry) * (size_t)len);

    int i = 0, j = mid - lo, k = lo;
    int left_end = mid - lo, right_end = len;

    while (i < left_end && j < right_end) {
        int c;
        if (has_cmp) {
            bool ok = true;
            c = entry_cmp_fn(vm, cmp_fn, &tmp[i], &tmp[j], reverse, &ok);
            if (!ok) return false;
        } else {
            /* Stable: equal → take left first (i < j means orig[i] < orig[j]) */
            c = entry_cmp_key(&tmp[i], &tmp[j], reverse, true);
        }
        if (c <= 0) arr[k++] = tmp[i++];
        else        arr[k++] = tmp[j++];
    }
    while (i < left_end)  arr[k++] = tmp[i++];
    while (j < right_end) arr[k++] = tmp[j++];
    return true;
}

static bool stable_sort_impl(MsVM* vm, SortEntry* arr, int n,
                             MsValue cmp_fn, MsValue key_fn, bool reverse) {
    /* Small: insertion sort is already stable */
    if (n <= 16) return insertion_sort(vm, arr, 0, n, cmp_fn, key_fn, reverse, true);

    SortEntry* tmp = (SortEntry*)malloc(sizeof(SortEntry) * (size_t)n);
    if (!tmp) abort();

    /* Bottom-up merge: run size starts at 8 */
    for (int run = 8; run < n; run *= 2) {
        ;
    }
    /* Actually: insertion-sort runs of 16, then merge */
    const int RUN = 16;
    for (int i = 0; i < n; i += RUN) {
        int end = i + RUN < n ? i + RUN : n;
        if (!insertion_sort(vm, arr, i, end, cmp_fn, key_fn, reverse, true)) {
            free(tmp);
            return false;
        }
    }
    for (int size = RUN; size < n; size *= 2) {
        for (int lo = 0; lo < n; lo += 2 * size) {
            int mid = lo + size;
            int hi  = lo + 2 * size;
            if (mid >= n) break;
            if (hi > n) hi = n;
            if (!merge_arrays(vm, arr, tmp, lo, mid, hi, cmp_fn, key_fn, reverse)) {
                free(tmp);
                return false;
            }
        }
    }
    free(tmp);
    return true;
}

/* ------------------------------------------------------------------ */
/* Introsort (unstable): quicksort + heap fallback                     */
/* ------------------------------------------------------------------ */

static int median3(MsVM* vm, SortEntry* arr, int a, int b, int c,
                   MsValue cmp_fn, bool reverse, bool* ok) {
    bool has_cmp = !MS_IS_NIL(cmp_fn);
    int ab, bc;
    if (has_cmp) {
        ab = entry_cmp_fn(vm, cmp_fn, &arr[a], &arr[b], reverse, ok); if (!*ok) return a;
        bc = entry_cmp_fn(vm, cmp_fn, &arr[b], &arr[c], reverse, ok); if (!*ok) return a;
    } else {
        ab = entry_cmp_key(&arr[a], &arr[b], reverse, false);
        bc = entry_cmp_key(&arr[b], &arr[c], reverse, false);
    }
    if (ab <= 0 && bc <= 0) return b;
    if (ab >= 0 && bc >= 0) return b;
    int ac = has_cmp ? entry_cmp_fn(vm, cmp_fn, &arr[a], &arr[c], reverse, ok)
                     : entry_cmp_key(&arr[a], &arr[c], reverse, false);
    if (!*ok) return a;
    return (ac <= 0) ? c : a;
}

static void heap_sift_down(MsVM* vm, SortEntry* arr, int i, int n,
                           MsValue cmp_fn, bool reverse, bool* ok) {
    bool has_cmp = !MS_IS_NIL(cmp_fn);
    while (true) {
        int largest = i;
        int l = 2 * i + 1, r = 2 * i + 2;
        int cl = -1, cr = -1;
        if (l < n) {
            cl = has_cmp ? entry_cmp_fn(vm, cmp_fn, &arr[l], &arr[largest], reverse, ok)
                         : entry_cmp_key(&arr[l], &arr[largest], reverse, false);
            if (!*ok) return;
            if (cl > 0) largest = l;
        }
        if (r < n) {
            cr = has_cmp ? entry_cmp_fn(vm, cmp_fn, &arr[r], &arr[largest], reverse, ok)
                         : entry_cmp_key(&arr[r], &arr[largest], reverse, false);
            if (!*ok) return;
            if (cr > 0) largest = r;
        }
        (void)cl; (void)cr;
        if (largest == i) break;
        SortEntry tmp = arr[i]; arr[i] = arr[largest]; arr[largest] = tmp;
        i = largest;
    }
}

static void heap_sort(MsVM* vm, SortEntry* arr, int lo, int hi,
                      MsValue cmp_fn, bool reverse, bool* ok) {
    int n = hi - lo;
    SortEntry* a = arr + lo;
    for (int i = n / 2 - 1; i >= 0; i--)
        heap_sift_down(vm, a, i, n, cmp_fn, reverse, ok);
    for (int i = n - 1; i > 0; i--) {
        SortEntry tmp = a[0]; a[0] = a[i]; a[i] = tmp;
        heap_sift_down(vm, a, 0, i, cmp_fn, reverse, ok);
    }
}

/* limit controls max recursion depth (log2(n)*2) */
static bool intro_sort_impl(MsVM* vm, SortEntry* arr, int lo, int hi,
                            int limit, MsValue cmp_fn, bool reverse) {
    bool has_cmp = !MS_IS_NIL(cmp_fn);
    while (hi - lo > 16) {
        if (limit == 0) {
            bool ok = true;
            heap_sort(vm, arr, lo, hi, cmp_fn, reverse, &ok);
            return ok;
        }
        limit--;
        bool ok = true;
        int m = median3(vm, arr, lo, lo + (hi - lo) / 2, hi - 1, cmp_fn, reverse, &ok);
        if (!ok) return false;
        SortEntry pivot = arr[m];
        /* partition */
        int i = lo, j = hi - 1;
        arr[m] = arr[lo];
        arr[lo] = pivot;
        while (true) {
            int cl, cr;
            cl = has_cmp ? entry_cmp_fn(vm, cmp_fn, &arr[i], &pivot, reverse, &ok)
                         : entry_cmp_key(&arr[i], &pivot, reverse, false);
            if (!ok) return false;
            while (cl < 0 && i < j) { i++; cl = has_cmp ? entry_cmp_fn(vm, cmp_fn, &arr[i], &pivot, reverse, &ok) : entry_cmp_key(&arr[i], &pivot, reverse, false); if (!ok) return false; }
            cr = has_cmp ? entry_cmp_fn(vm, cmp_fn, &arr[j], &pivot, reverse, &ok)
                         : entry_cmp_key(&arr[j], &pivot, reverse, false);
            if (!ok) return false;
            while (cr > 0 && j > i) { j--; cr = has_cmp ? entry_cmp_fn(vm, cmp_fn, &arr[j], &pivot, reverse, &ok) : entry_cmp_key(&arr[j], &pivot, reverse, false); if (!ok) return false; }
            if (i >= j) break;
            SortEntry tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
            i++; j--;
        }
        /* recurse on smaller partition, iterate on larger */
        if (i - lo < hi - j) {
            if (!intro_sort_impl(vm, arr, lo, i, limit, cmp_fn, reverse)) return false;
            lo = j;
        } else {
            if (!intro_sort_impl(vm, arr, j, hi, limit, cmp_fn, reverse)) return false;
            hi = i;
        }
    }
    return insertion_sort(vm, arr, lo, hi, cmp_fn, MS_NIL_VAL(), reverse, false);
}

static int ilog2(int n) {
    int k = 0;
    while (n > 1) { n >>= 1; k++; }
    return k;
}

static bool unstable_sort_impl(MsVM* vm, SortEntry* arr, int n,
                               MsValue cmp_fn, bool reverse) {
    if (n <= 1) return true;
    int limit = ilog2(n) * 2;
    return intro_sort_impl(vm, arr, 0, n, limit, cmp_fn, reverse);
}

/* ------------------------------------------------------------------ */
/* Parse shared arguments: list, key, reverse, cmp                    */
/* (key and reverse are positional; cmp is optional 4th arg)          */
/* ------------------------------------------------------------------ */

static bool parse_sort_args(MsVM* vm, int argc, MsValue* argv,
                            MsValue* key_fn, bool* reverse, MsValue* cmp_fn) {
    *key_fn = MS_NIL_VAL();
    *reverse = false;
    *cmp_fn = MS_NIL_VAL();

    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sort: first argument must be a list");
        return false;
    }
    if (argc >= 2 && !MS_IS_NIL(argv[1])) {
        if (!is_callable(argv[1])) {
            ms_vm_runtime_error(vm, "sort: key must be a function or nil");
            return false;
        }
        *key_fn = argv[1];
    }
    if (argc >= 3 && MS_IS_BOOL(argv[2]))
        *reverse = MS_AS_BOOL(argv[2]);
    if (argc >= 4 && !MS_IS_NIL(argv[3])) {
        if (!is_callable(argv[3])) {
            ms_vm_runtime_error(vm, "sort: cmp must be a function or nil");
            return false;
        }
        *cmp_fn = argv[3];
        /* when cmp is given, key is ignored per spec */
        *key_fn = MS_NIL_VAL();
    }
    return true;
}

/* Build a SortEntry array from a list. Returns NULL on OOM. */
static SortEntry* make_entries(MsObjList* src) {
    int n = src->items.count;
    if (n == 0) return NULL;
    SortEntry* arr = (SortEntry*)malloc(sizeof(SortEntry) * (size_t)n);
    if (!arr) abort();
    for (int i = 0; i < n; i++) {
        arr[i].item = src->items.data[i];
        arr[i].key  = arr[i].item;
        arr[i].orig = i;
    }
    return arr;
}

/* ------------------------------------------------------------------ */
/* sort.sort — in-place unstable sort                                  */
/* ------------------------------------------------------------------ */

static MsValue srt_sort(MsVM* vm, int argc, MsValue* argv) {
    MsValue key_fn, cmp_fn;
    bool reverse;
    if (!parse_sort_args(vm, argc, argv, &key_fn, &reverse, &cmp_fn)) return MS_NIL_VAL();

    MsObjList* list = MS_AS_LIST(argv[0]);
    int n = list->items.count;
    if (n <= 1) return MS_NIL_VAL();

    SortEntry* arr = make_entries(list);
    if (!compute_keys(vm, arr, n, key_fn)) { free(arr); return MS_NIL_VAL(); }

    if (!unstable_sort_impl(vm, arr, n, cmp_fn, reverse)) { free(arr); return MS_NIL_VAL(); }

    for (int i = 0; i < n; i++) list->items.data[i] = arr[i].item;
    free(arr);
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* sort.stable_sort — in-place stable sort                             */
/* ------------------------------------------------------------------ */

static MsValue srt_stable_sort(MsVM* vm, int argc, MsValue* argv) {
    MsValue key_fn, cmp_fn;
    bool reverse;
    if (!parse_sort_args(vm, argc, argv, &key_fn, &reverse, &cmp_fn)) return MS_NIL_VAL();

    MsObjList* list = MS_AS_LIST(argv[0]);
    int n = list->items.count;
    if (n <= 1) return MS_NIL_VAL();

    SortEntry* arr = make_entries(list);
    if (!compute_keys(vm, arr, n, key_fn)) { free(arr); return MS_NIL_VAL(); }

    if (!stable_sort_impl(vm, arr, n, cmp_fn, key_fn, reverse)) { free(arr); return MS_NIL_VAL(); }

    for (int i = 0; i < n; i++) list->items.data[i] = arr[i].item;
    free(arr);
    return MS_NIL_VAL();
}

/* ------------------------------------------------------------------ */
/* sort.sorted — return sorted copy                                    */
/* ------------------------------------------------------------------ */

static MsValue srt_sorted(MsVM* vm, int argc, MsValue* argv) {
    MsValue key_fn, cmp_fn;
    bool reverse;
    if (!parse_sort_args(vm, argc, argv, &key_fn, &reverse, &cmp_fn)) return MS_NIL_VAL();

    MsObjList* src = MS_AS_LIST(argv[0]);
    int n = src->items.count;
    MsObjList* dst = ms_obj_list_new(vm);
    if (n == 0) return MS_OBJ_VAL(dst);

    SortEntry* arr = make_entries(src);
    if (!compute_keys(vm, arr, n, key_fn)) { free(arr); return MS_NIL_VAL(); }

    if (!stable_sort_impl(vm, arr, n, cmp_fn, key_fn, reverse)) { free(arr); return MS_NIL_VAL(); }

    for (int i = 0; i < n; i++) ms_value_array_push(&dst->items, arr[i].item);
    free(arr);
    return MS_OBJ_VAL(dst);
}

/* ------------------------------------------------------------------ */
/* sort.is_sorted — ascending order check                              */
/* ------------------------------------------------------------------ */

static MsValue srt_is_sorted(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sort.is_sorted: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    int n = src->items.count;
    if (n <= 1) return MS_BOOL_VAL(true);

    MsValue key_fn = (argc >= 2 && is_callable(argv[1])) ? argv[1] : MS_NIL_VAL();
    bool has_key = !MS_IS_NIL(key_fn);

    for (int i = 1; i < n; i++) {
        MsValue ka, kb;
        if (has_key) {
            if (!call1(vm, key_fn, src->items.data[i - 1], &ka)) return MS_NIL_VAL();
            if (!call1(vm, key_fn, src->items.data[i],     &kb)) return MS_NIL_VAL();
        } else {
            ka = src->items.data[i - 1];
            kb = src->items.data[i];
        }
        if (value_cmp_natural(ka, kb) > 0) return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

/* ------------------------------------------------------------------ */
/* sort.is_sorted_desc — descending order check                        */
/* ------------------------------------------------------------------ */

static MsValue srt_is_sorted_desc(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sort.is_sorted_desc: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* src = MS_AS_LIST(argv[0]);
    int n = src->items.count;
    if (n <= 1) return MS_BOOL_VAL(true);

    MsValue key_fn = (argc >= 2 && is_callable(argv[1])) ? argv[1] : MS_NIL_VAL();
    bool has_key = !MS_IS_NIL(key_fn);

    for (int i = 1; i < n; i++) {
        MsValue ka, kb;
        if (has_key) {
            if (!call1(vm, key_fn, src->items.data[i - 1], &ka)) return MS_NIL_VAL();
            if (!call1(vm, key_fn, src->items.data[i],     &kb)) return MS_NIL_VAL();
        } else {
            ka = src->items.data[i - 1];
            kb = src->items.data[i];
        }
        if (value_cmp_natural(ka, kb) < 0) return MS_BOOL_VAL(false);
    }
    return MS_BOOL_VAL(true);
}

/* ------------------------------------------------------------------ */
/* Binary search helpers (list assumed sorted ascending by key)        */
/* ------------------------------------------------------------------ */

/*
 * Get the effective key for element at index i.
 * If key_fn is nil, returns the element itself.
 * Returns false on callback error.
 */
static bool get_key(MsVM* vm, MsObjList* list, int i, MsValue key_fn, MsValue* out) {
    if (MS_IS_NIL(key_fn)) {
        *out = list->items.data[i];
        return true;
    }
    return call1(vm, key_fn, list->items.data[i], out);
}

/*
 * bisect_left: returns index of first element where key >= value.
 * On error, returns -2 (sentinel).
 */
static int bisect_left_impl(MsVM* vm, MsObjList* list, MsValue value, MsValue key_fn) {
    int lo = 0, hi = list->items.count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        MsValue k;
        if (!get_key(vm, list, mid, key_fn, &k)) return -2;
        if (value_cmp_natural(k, value) < 0) lo = mid + 1;
        else                                  hi = mid;
    }
    return lo;
}

/*
 * bisect_right: returns index of first element where key > value.
 */
static int bisect_right_impl(MsVM* vm, MsObjList* list, MsValue value, MsValue key_fn) {
    int lo = 0, hi = list->items.count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        MsValue k;
        if (!get_key(vm, list, mid, key_fn, &k)) return -2;
        if (value_cmp_natural(k, value) <= 0) lo = mid + 1;
        else                                   hi = mid;
    }
    return lo;
}

/* ------------------------------------------------------------------ */
/* sort.binary_search — index or -1                                    */
/* ------------------------------------------------------------------ */

static MsValue srt_binary_search(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sort.binary_search: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* list = MS_AS_LIST(argv[0]);
    MsValue value   = argv[1];
    MsValue key_fn  = (argc >= 3 && is_callable(argv[2])) ? argv[2] : MS_NIL_VAL();

    int idx = bisect_left_impl(vm, list, value, key_fn);
    if (idx == -2) return MS_NIL_VAL(); /* callback error */
    if (idx < list->items.count) {
        MsValue k;
        if (!get_key(vm, list, idx, key_fn, &k)) return MS_NIL_VAL();
        if (value_cmp_natural(k, value) == 0) return MS_INT_VAL((ms_i64)idx);
    }
    return MS_INT_VAL(-1);
}

/* ------------------------------------------------------------------ */
/* sort.bisect_left                                                     */
/* ------------------------------------------------------------------ */

static MsValue srt_bisect_left(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sort.bisect_left: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* list = MS_AS_LIST(argv[0]);
    MsValue value   = argv[1];
    MsValue key_fn  = (argc >= 3 && is_callable(argv[2])) ? argv[2] : MS_NIL_VAL();

    int idx = bisect_left_impl(vm, list, value, key_fn);
    if (idx == -2) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)idx);
}

/* ------------------------------------------------------------------ */
/* sort.bisect_right                                                    */
/* ------------------------------------------------------------------ */

static MsValue srt_bisect_right(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sort.bisect_right: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* list = MS_AS_LIST(argv[0]);
    MsValue value   = argv[1];
    MsValue key_fn  = (argc >= 3 && is_callable(argv[2])) ? argv[2] : MS_NIL_VAL();

    int idx = bisect_right_impl(vm, list, value, key_fn);
    if (idx == -2) return MS_NIL_VAL();
    return MS_INT_VAL((ms_i64)idx);
}

/* ------------------------------------------------------------------ */
/* sort.search_range — returns [lo, hi) as a two-element list          */
/* ------------------------------------------------------------------ */

static MsValue srt_search_range(MsVM* vm, int argc, MsValue* argv) {
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "sort.search_range: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* list = MS_AS_LIST(argv[0]);
    MsValue value   = argv[1];
    MsValue key_fn  = (argc >= 3 && is_callable(argv[2])) ? argv[2] : MS_NIL_VAL();

    int lo = bisect_left_impl(vm, list, value, key_fn);
    if (lo == -2) return MS_NIL_VAL();
    int hi = bisect_right_impl(vm, list, value, key_fn);
    if (hi == -2) return MS_NIL_VAL();

    MsValue pair[2] = { MS_INT_VAL((ms_i64)lo), MS_INT_VAL((ms_i64)hi) };
    return MS_OBJ_VAL(ms_obj_list_from_array(vm, pair, 2));
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_sort_defs[] = {
    /* sorting */
    {"sort",         srt_sort,          -1},
    {"stable_sort",  srt_stable_sort,   -1},
    {"sorted",       srt_sorted,        -1},
    /* query */
    {"is_sorted",       srt_is_sorted,      -1},
    {"is_sorted_desc",  srt_is_sorted_desc, -1},
    /* binary search */
    {"binary_search",   srt_binary_search,  -1},
    {"bisect_left",     srt_bisect_left,    -1},
    {"bisect_right",    srt_bisect_right,   -1},
    {"search_range",    srt_search_range,   -1},
    {NULL, NULL, 0}
};

void ms_module_sort_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_sort_defs);
}
