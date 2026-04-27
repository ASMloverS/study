#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/vtable.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- helpers ---- */

static int normalize_index(int idx, int len) {
    if (idx < 0) idx += len;
    if (idx < 0) idx = 0;
    if (idx > len) idx = len;
    return idx;
}

/* ---- string methods ---- */

static bool string_invoke(MsVM* vm, MsObjString* s,
                           MsObjString* method, int argc,
                           MsValue* argv, MsValue* out) {
    const char* name = method->data;

    if (strcmp(name, "len") == 0) {
        *out = MS_INT_VAL(s->length);
        return true;
    }
    if (strcmp(name, "upper") == 0) {
        char* buf = (char*)malloc((size_t)s->length + 1);
        if (!buf) abort();
        for (int i = 0; i < s->length; i++) buf[i] = (char)toupper((unsigned char)s->data[i]);
        buf[s->length] = '\0';
        *out = MS_OBJ_VAL(ms_obj_string_take(vm, buf, s->length));
        return true;
    }
    if (strcmp(name, "lower") == 0) {
        char* buf = (char*)malloc((size_t)s->length + 1);
        if (!buf) abort();
        for (int i = 0; i < s->length; i++) buf[i] = (char)tolower((unsigned char)s->data[i]);
        buf[s->length] = '\0';
        *out = MS_OBJ_VAL(ms_obj_string_take(vm, buf, s->length));
        return true;
    }
    if (strcmp(name, "contains") == 0) {
        if (argc < 1 || !MS_IS_STRING(argv[0])) return false;
        MsObjString* needle = MS_AS_STRING(argv[0]);
        *out = MS_BOOL_VAL(strstr(s->data, needle->data) != NULL);
        return true;
    }
    if (strcmp(name, "starts_with") == 0) {
        if (argc < 1 || !MS_IS_STRING(argv[0])) return false;
        MsObjString* pre = MS_AS_STRING(argv[0]);
        bool r = (s->length >= pre->length) &&
                  (memcmp(s->data, pre->data, (size_t)pre->length) == 0);
        *out = MS_BOOL_VAL(r);
        return true;
    }
    if (strcmp(name, "ends_with") == 0) {
        if (argc < 1 || !MS_IS_STRING(argv[0])) return false;
        MsObjString* suf = MS_AS_STRING(argv[0]);
        bool r = (s->length >= suf->length) &&
                  (memcmp(s->data + s->length - suf->length,
                           suf->data, (size_t)suf->length) == 0);
        *out = MS_BOOL_VAL(r);
        return true;
    }
    if (strcmp(name, "index_of") == 0) {
        if (argc < 1 || !MS_IS_STRING(argv[0])) return false;
        MsObjString* needle = MS_AS_STRING(argv[0]);
        char* p = strstr(s->data, needle->data);
        *out = MS_INT_VAL(p ? (ms_i64)(p - s->data) : -1);
        return true;
    }
    if (strcmp(name, "trim") == 0) {
        int start = 0, end = s->length;
        while (start < end && isspace((unsigned char)s->data[start])) start++;
        while (end > start && isspace((unsigned char)s->data[end - 1])) end--;
        *out = MS_OBJ_VAL(ms_obj_string_copy(vm, s->data + start, end - start));
        return true;
    }
    if (strcmp(name, "slice") == 0) {
        int st = (argc >= 1 && MS_IS_INT(argv[0])) ? (int)MS_AS_INT(argv[0]) : 0;
        int en = (argc >= 2 && MS_IS_INT(argv[1])) ? (int)MS_AS_INT(argv[1]) : s->length;
        st = normalize_index(st, s->length);
        en = normalize_index(en, s->length);
        if (en < st) en = st;
        *out = MS_OBJ_VAL(ms_obj_string_copy(vm, s->data + st, en - st));
        return true;
    }
    if (strcmp(name, "split") == 0) {
        if (argc < 1 || !MS_IS_STRING(argv[0])) return false;
        MsObjString* sep = MS_AS_STRING(argv[0]);
        MsObjList* list = ms_obj_list_new(vm);
        *out = MS_OBJ_VAL(list);
        if (sep->length == 0) {
            for (int i = 0; i < s->length; i++) {
                MsObjString* ch = ms_obj_string_copy(vm, s->data + i, 1);
                ms_value_array_push(&list->items, MS_OBJ_VAL(ch));
            }
            return true;
        }
        const char* cur = s->data;
        const char* end_ptr = s->data + s->length;
        while (cur <= end_ptr) {
            char* found = strstr(cur, sep->data);
            int part_len = found ? (int)(found - cur) : (int)(end_ptr - cur);
            MsObjString* part = ms_obj_string_copy(vm, cur, part_len);
            ms_value_array_push(&list->items, MS_OBJ_VAL(part));
            if (!found) break;
            cur = found + sep->length;
        }
        return true;
    }
    if (strcmp(name, "replace") == 0) {
        if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) return false;
        MsObjString* old_s = MS_AS_STRING(argv[0]);
        MsObjString* new_s = MS_AS_STRING(argv[1]);
        MsObjStringBuilder* sb = ms_obj_sb_new(vm);
        *out = MS_OBJ_VAL(sb);
        const char* cur = s->data;
        while (*cur) {
            char* found = old_s->length > 0 ? strstr(cur, old_s->data) : NULL;
            if (!found) { ms_obj_sb_append(sb, cur, (int)strlen(cur)); break; }
            ms_obj_sb_append(sb, cur, (int)(found - cur));
            ms_obj_sb_append(sb, new_s->data, new_s->length);
            cur = found + old_s->length;
        }
        *out = MS_OBJ_VAL(ms_obj_sb_to_string(vm, sb));
        return true;
    }
    return false;
}

/* ---- list comparison for sort ---- */

static int list_compare(const void* a, const void* b) {
    const MsValue* va = (const MsValue*)a;
    const MsValue* vb = (const MsValue*)b;
    if (MS_IS_INT(*va) && MS_IS_INT(*vb)) {
        ms_i64 da = MS_AS_INT(*va), db = MS_AS_INT(*vb);
        return da < db ? -1 : da > db ? 1 : 0;
    }
    double da = MS_IS_INT(*va) ? (double)MS_AS_INT(*va) : MS_AS_NUMBER(*va);
    double db = MS_IS_INT(*vb) ? (double)MS_AS_INT(*vb) : MS_AS_NUMBER(*vb);
    return da < db ? -1 : da > db ? 1 : 0;
}

/* ---- list methods ---- */

static bool list_invoke(MsVM* vm, MsObjList* list,
                         MsObjString* method, int argc,
                         MsValue* argv, MsValue* out) {
    const char* name = method->data;

    if (strcmp(name, "len") == 0) {
        *out = MS_INT_VAL(list->items.count);
        return true;
    }
    if (strcmp(name, "push") == 0) {
        if (argc < 1) return false;
        ms_value_array_push(&list->items, argv[0]);
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "pop") == 0) {
        if (list->items.count == 0) { *out = MS_NIL_VAL(); return true; }
        *out = list->items.data[--list->items.count];
        return true;
    }
    if (strcmp(name, "contains") == 0) {
        if (argc < 1) return false;
        for (int i = 0; i < list->items.count; i++) {
            if (ms_value_equals(list->items.data[i], argv[0])) {
                *out = MS_BOOL_VAL(true); return true;
            }
        }
        *out = MS_BOOL_VAL(false);
        return true;
    }
    if (strcmp(name, "index_of") == 0) {
        if (argc < 1) return false;
        for (int i = 0; i < list->items.count; i++) {
            if (ms_value_equals(list->items.data[i], argv[0])) {
                *out = MS_INT_VAL(i); return true;
            }
        }
        *out = MS_INT_VAL(-1);
        return true;
    }
    if (strcmp(name, "remove") == 0) {
        if (argc < 1 || !MS_IS_INT(argv[0])) return false;
        int idx = (int)MS_AS_INT(argv[0]);
        if (idx < 0) idx += list->items.count;
        if (idx < 0 || idx >= list->items.count) { *out = MS_NIL_VAL(); return true; }
        *out = list->items.data[idx];
        memmove(&list->items.data[idx], &list->items.data[idx + 1],
                sizeof(MsValue) * (size_t)(list->items.count - idx - 1));
        list->items.count--;
        return true;
    }
    if (strcmp(name, "sort") == 0) {
        qsort(list->items.data, (size_t)list->items.count,
              sizeof(MsValue), list_compare);
        *out = MS_OBJ_VAL(list);
        return true;
    }
    if (strcmp(name, "reverse") == 0) {
        int i = 0, j = list->items.count - 1;
        while (i < j) {
            MsValue tmp = list->items.data[i];
            list->items.data[i++] = list->items.data[j];
            list->items.data[j--] = tmp;
        }
        *out = MS_OBJ_VAL(list);
        return true;
    }
    if (strcmp(name, "slice") == 0) {
        int st = (argc >= 1 && MS_IS_INT(argv[0])) ? (int)MS_AS_INT(argv[0]) : 0;
        int en = (argc >= 2 && MS_IS_INT(argv[1])) ? (int)MS_AS_INT(argv[1]) : list->items.count;
        st = normalize_index(st, list->items.count);
        en = normalize_index(en, list->items.count);
        if (en < st) en = st;
        MsObjList* result = ms_obj_list_new(vm);
        for (int i = st; i < en; i++)
            ms_value_array_push(&result->items, list->items.data[i]);
        *out = MS_OBJ_VAL(result);
        return true;
    }
    if (strcmp(name, "map") == 0) {
        if (argc < 1) return false;
        MsObjList* result = ms_obj_list_new(vm);
        *out = MS_OBJ_VAL(result);
        for (int i = 0; i < list->items.count; i++) {
            MsValue item = list->items.data[i];
            MsValue mapped;
            MsInterpretResult r = ms_vm_call_sync(vm, argv[0], &item, 1, &mapped);
            if (r != MS_INTERPRET_OK) return false;
            ms_value_array_push(&result->items, mapped);
        }
        return true;
    }
    if (strcmp(name, "filter") == 0) {
        if (argc < 1) return false;
        MsObjList* result = ms_obj_list_new(vm);
        *out = MS_OBJ_VAL(result);
        for (int i = 0; i < list->items.count; i++) {
            MsValue item = list->items.data[i];
            MsValue pred;
            MsInterpretResult r = ms_vm_call_sync(vm, argv[0], &item, 1, &pred);
            if (r != MS_INTERPRET_OK) return false;
            if (ms_value_is_truthy(pred))
                ms_value_array_push(&result->items, item);
        }
        return true;
    }
    if (strcmp(name, "join") == 0) {
        MsObjString* sep = (argc >= 1 && MS_IS_STRING(argv[0]))
                           ? MS_AS_STRING(argv[0])
                           : ms_obj_string_copy(vm, "", 0);
        MsObjStringBuilder* sb = ms_obj_sb_new(vm);
        for (int i = 0; i < list->items.count; i++) {
            if (i > 0) ms_obj_sb_append(sb, sep->data, sep->length);
            MsValue item = list->items.data[i];
            if (MS_IS_STRING(item)) {
                MsObjString* ss = MS_AS_STRING(item);
                ms_obj_sb_append(sb, ss->data, ss->length);
            } else {
                char* cs = ms_value_to_cstring(item);
                ms_obj_sb_append(sb, cs, (int)strlen(cs));
                free(cs);
            }
        }
        *out = MS_OBJ_VAL(ms_obj_sb_to_string(vm, sb));
        return true;
    }
    return false;
}

/* ---- map methods ---- */

static bool map_invoke(MsVM* vm, MsObjMap* map,
                        MsObjString* method, int argc,
                        MsValue* argv, MsValue* out) {
    const char* name = method->data;

    if (strcmp(name, "len") == 0) {
        int cnt = 0;
        for (int i = 0; i < map->table.capacity; i++)
            if (map->table.entries[i].used) cnt++;
        *out = MS_INT_VAL(cnt);
        return true;
    }
    if (strcmp(name, "has") == 0) {
        if (argc < 1) return false;
        MsValue dummy;
        *out = MS_BOOL_VAL(ms_vtable_get(&map->table, argv[0], &dummy));
        return true;
    }
    if (strcmp(name, "remove") == 0) {
        if (argc < 1) return false;
        *out = MS_BOOL_VAL(ms_vtable_delete(&map->table, argv[0]));
        return true;
    }
    if (strcmp(name, "keys") == 0) {
        MsObjList* list = ms_obj_list_new(vm);
        for (int i = 0; i < map->table.capacity; i++) {
            if (map->table.entries[i].used)
                ms_value_array_push(&list->items, map->table.entries[i].key);
        }
        *out = MS_OBJ_VAL(list);
        return true;
    }
    if (strcmp(name, "values") == 0) {
        MsObjList* list = ms_obj_list_new(vm);
        for (int i = 0; i < map->table.capacity; i++) {
            if (map->table.entries[i].used)
                ms_value_array_push(&list->items, map->table.entries[i].value);
        }
        *out = MS_OBJ_VAL(list);
        return true;
    }
    (void)argc; (void)argv;
    return false;
}

/* ---- tuple methods ---- */

static bool tuple_invoke(MsVM* vm, MsObjTuple* tup,
                          MsObjString* method, int argc,
                          MsValue* argv, MsValue* out) {
    (void)vm;
    const char* name = method->data;

    if (strcmp(name, "len") == 0) {
        *out = MS_INT_VAL(tup->count);
        return true;
    }
    if (strcmp(name, "contains") == 0) {
        if (argc < 1) return false;
        for (int i = 0; i < tup->count; i++) {
            if (ms_value_equals(tup->items[i], argv[0])) {
                *out = MS_BOOL_VAL(true); return true;
            }
        }
        *out = MS_BOOL_VAL(false);
        return true;
    }
    (void)argc; (void)argv;
    return false;
}

/* ---- public dispatch ---- */

bool ms_builtin_invoke(MsVM* vm, MsValue receiver, MsObjString* method,
                        int argc, MsValue* argv, MsValue* out) {
    if (MS_IS_STRING(receiver))
        return string_invoke(vm, MS_AS_STRING(receiver), method, argc, argv, out);
    if (MS_IS_LIST(receiver))
        return list_invoke(vm, MS_AS_LIST(receiver), method, argc, argv, out);
    if (MS_IS_MAP(receiver))
        return map_invoke(vm, MS_AS_MAP(receiver), method, argc, argv, out);
    if (MS_IS_TUPLE(receiver))
        return tuple_invoke(vm, MS_AS_TUPLE(receiver), method, argc, argv, out);
    return false;
}
