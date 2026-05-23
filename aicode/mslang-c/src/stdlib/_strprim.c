/* src/stdlib/_strprim.c - STDLIB-13: strings module (C primitive layer)
 *
 * Implements all functions that built-in string methods don't cover:
 * join, repeat, fields, split_n, replace(n), trim(chars), trim_left,
 * trim_right, last_index, count, pad_left, pad_right, center, title,
 * capitalize, is_alpha/digit/alnum/space/upper/lower, to_bytes,
 * from_bytes, to_buffer, is_empty, to_upper, to_lower, contains,
 * has_prefix, has_suffix, index, split, replace_all, trim_space.
 *
 * Registered under the module name "strings".
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/stdlib/objbuffer.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static bool require_string(MsVM* vm, MsValue v, const char* fn, MsObjString** out) {
    if (!MS_IS_STRING(v)) {
        ms_vm_runtime_error(vm, "%s: expected string", fn);
        return false;
    }
    *out = MS_AS_STRING(v);
    return true;
}

static bool require_int(MsVM* vm, MsValue v, const char* fn, ms_i64* out) {
    if (!MS_IS_INT(v)) {
        ms_vm_runtime_error(vm, "%s: expected int", fn);
        return false;
    }
    *out = MS_AS_INT(v);
    return true;
}

/* ------------------------------------------------------------------ */
/* join(list, sep) → str                                               */
/* ------------------------------------------------------------------ */

static MsValue str_join(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "strings.join: expected list");
        return MS_NIL_VAL();
    }
    MsObjString* sep;
    if (!require_string(vm, argv[1], "strings.join", &sep)) return MS_NIL_VAL();

    MsObjList* list = MS_AS_LIST(argv[0]);
    MsObjStringBuilder* sb = ms_obj_sb_new(vm);
    for (int i = 0; i < list->items.count; i++) {
        if (i > 0) ms_obj_sb_append(sb, sep->data, sep->length);
        MsValue item = list->items.data[i];
        if (MS_IS_STRING(item)) {
            MsObjString* s = MS_AS_STRING(item);
            ms_obj_sb_append(sb, s->data, s->length);
        } else {
            char* cs = ms_value_to_cstring(item);
            ms_obj_sb_append(sb, cs, (int)strlen(cs));
            free(cs);
        }
    }
    return MS_OBJ_VAL(ms_obj_sb_to_string(vm, sb));
}

/* ------------------------------------------------------------------ */
/* repeat(s, n) → str                                                  */
/* ------------------------------------------------------------------ */

static MsValue str_repeat(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    ms_i64 n;
    if (!require_string(vm, argv[0], "strings.repeat", &s)) return MS_NIL_VAL();
    if (!require_int(vm, argv[1], "strings.repeat", &n)) return MS_NIL_VAL();
    if (n <= 0) return MS_OBJ_VAL(ms_obj_string_copy(vm, "", 0));

    int total = s->length * (int)n;
    char* buf = (char*)malloc((size_t)total + 1);
    if (!buf) abort();
    for (int i = 0; i < (int)n; i++)
        memcpy(buf + i * s->length, s->data, (size_t)s->length);
    buf[total] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, total));
}

/* ------------------------------------------------------------------ */
/* fields(s) → list[str]   split on consecutive whitespace            */
/* ------------------------------------------------------------------ */

static MsValue str_fields(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.fields", &s)) return MS_NIL_VAL();

    MsObjList* list = ms_obj_list_new(vm);
    MsValue out = MS_OBJ_VAL(list);
    const char* p = s->data;
    const char* end = p + s->length;
    while (p < end) {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end) break;
        const char* start = p;
        while (p < end && !isspace((unsigned char)*p)) p++;
        MsObjString* token = ms_obj_string_copy(vm, start, (int)(p - start));
        ms_value_array_push(&list->items, MS_OBJ_VAL(token));
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* split(s, sep) → list[str]  functional alias                        */
/* ------------------------------------------------------------------ */

static MsValue str_split(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    MsObjString* sep;
    if (!require_string(vm, argv[0], "strings.split", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.split", &sep)) return MS_NIL_VAL();

    MsObjList* list = ms_obj_list_new(vm);
    MsValue out = MS_OBJ_VAL(list);
    if (sep->length == 0) {
        for (int i = 0; i < s->length; i++) {
            MsObjString* ch = ms_obj_string_copy(vm, s->data + i, 1);
            ms_value_array_push(&list->items, MS_OBJ_VAL(ch));
        }
        return out;
    }
    const char* cur = s->data;
    const char* end = s->data + s->length;
    while (cur <= end) {
        char* found = strstr(cur, sep->data);
        int part_len = found ? (int)(found - cur) : (int)(end - cur);
        MsObjString* part = ms_obj_string_copy(vm, cur, part_len);
        ms_value_array_push(&list->items, MS_OBJ_VAL(part));
        if (!found) break;
        cur = found + sep->length;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* split_n(s, sep, n) → list[str]   at most n parts                  */
/* ------------------------------------------------------------------ */

static MsValue str_split_n(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    MsObjString* sep;
    ms_i64 n;
    if (!require_string(vm, argv[0], "strings.split_n", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.split_n", &sep)) return MS_NIL_VAL();
    if (!require_int(vm, argv[2], "strings.split_n", &n)) return MS_NIL_VAL();

    MsObjList* list = ms_obj_list_new(vm);
    MsValue out = MS_OBJ_VAL(list);
    if (n <= 0 || sep->length == 0) {
        /* fall back: add the whole string */
        ms_value_array_push(&list->items, MS_OBJ_VAL(ms_obj_string_copy(vm, s->data, s->length)));
        return out;
    }
    const char* cur = s->data;
    const char* end = s->data + s->length;
    int parts = 0;
    while (cur <= end) {
        if (parts + 1 >= (int)n) {
            /* last chunk: rest of string */
            ms_value_array_push(&list->items,
                MS_OBJ_VAL(ms_obj_string_copy(vm, cur, (int)(end - cur))));
            break;
        }
        char* found = strstr(cur, sep->data);
        int part_len = found ? (int)(found - cur) : (int)(end - cur);
        ms_value_array_push(&list->items,
            MS_OBJ_VAL(ms_obj_string_copy(vm, cur, part_len)));
        parts++;
        if (!found) break;
        cur = found + sep->length;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* contains / has_prefix / has_suffix / index (functional aliases)    */
/* ------------------------------------------------------------------ */

static MsValue str_contains(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* sub;
    if (!require_string(vm, argv[0], "strings.contains", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.contains", &sub)) return MS_NIL_VAL();
    return MS_BOOL_VAL(strstr(s->data, sub->data) != NULL);
}

static MsValue str_has_prefix(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* pre;
    if (!require_string(vm, argv[0], "strings.has_prefix", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.has_prefix", &pre)) return MS_NIL_VAL();
    bool r = (s->length >= pre->length) &&
              (memcmp(s->data, pre->data, (size_t)pre->length) == 0);
    return MS_BOOL_VAL(r);
}

static MsValue str_has_suffix(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* suf;
    if (!require_string(vm, argv[0], "strings.has_suffix", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.has_suffix", &suf)) return MS_NIL_VAL();
    bool r = (s->length >= suf->length) &&
              (memcmp(s->data + s->length - suf->length,
                      suf->data, (size_t)suf->length) == 0);
    return MS_BOOL_VAL(r);
}

static MsValue str_index(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* sub;
    if (!require_string(vm, argv[0], "strings.index", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.index", &sub)) return MS_NIL_VAL();
    char* p = strstr(s->data, sub->data);
    return MS_INT_VAL(p ? (ms_i64)(p - s->data) : -1);
}

/* ------------------------------------------------------------------ */
/* last_index(s, sub) → int                                           */
/* ------------------------------------------------------------------ */

static MsValue str_last_index(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* sub;
    if (!require_string(vm, argv[0], "strings.last_index", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.last_index", &sub)) return MS_NIL_VAL();
    if (sub->length == 0) return MS_INT_VAL(s->length);
    if (sub->length > s->length) return MS_INT_VAL(-1);
    ms_i64 found = -1;
    for (int i = 0; i <= s->length - sub->length; i++) {
        if (memcmp(s->data + i, sub->data, (size_t)sub->length) == 0)
            found = i;
    }
    return MS_INT_VAL(found);
}

/* ------------------------------------------------------------------ */
/* count(s, sub) → int  (non-overlapping)                             */
/* ------------------------------------------------------------------ */

static MsValue str_count(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* sub;
    if (!require_string(vm, argv[0], "strings.count", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.count", &sub)) return MS_NIL_VAL();
    if (sub->length == 0) return MS_INT_VAL(s->length + 1);
    int cnt = 0;
    const char* cur = s->data;
    while ((cur = strstr(cur, sub->data)) != NULL) {
        cnt++;
        cur += sub->length;
    }
    return MS_INT_VAL(cnt);
}

/* ------------------------------------------------------------------ */
/* replace(s, old, new, n) → str   n=-1 means all                    */
/* ------------------------------------------------------------------ */

static MsValue str_replace(MsVM* vm, int argc, MsValue* argv) {
    MsObjString* s; MsObjString* old_s; MsObjString* new_s;
    if (!require_string(vm, argv[0], "strings.replace", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.replace", &old_s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[2], "strings.replace", &new_s)) return MS_NIL_VAL();
    ms_i64 n = -1;
    if (argc >= 4 && MS_IS_INT(argv[3])) n = MS_AS_INT(argv[3]);

    if (old_s->length == 0 || n == 0)
        return MS_OBJ_VAL(ms_obj_string_copy(vm, s->data, s->length));

    MsObjStringBuilder* sb = ms_obj_sb_new(vm);
    const char* cur = s->data;
    ms_i64 replaced = 0;
    while (*cur) {
        if (n != -1 && replaced >= n) {
            ms_obj_sb_append(sb, cur, (int)strlen(cur));
            break;
        }
        char* found = strstr(cur, old_s->data);
        if (!found) { ms_obj_sb_append(sb, cur, (int)strlen(cur)); break; }
        ms_obj_sb_append(sb, cur, (int)(found - cur));
        ms_obj_sb_append(sb, new_s->data, new_s->length);
        cur = found + old_s->length;
        replaced++;
    }
    return MS_OBJ_VAL(ms_obj_sb_to_string(vm, sb));
}

static MsValue str_replace_all(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    /* Reuse str_replace with n=-1 */
    MsValue argv4[4] = { argv[0], argv[1], argv[2], MS_INT_VAL(-1) };
    return str_replace(vm, 4, argv4);
}

/* ------------------------------------------------------------------ */
/* trim(s, chars) / trim_left / trim_right                            */
/* ------------------------------------------------------------------ */

static MsValue str_trim_chars(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* chars;
    if (!require_string(vm, argv[0], "strings.trim", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.trim", &chars)) return MS_NIL_VAL();
    int lo = 0, hi = s->length;
    while (lo < hi && memchr(chars->data, (unsigned char)s->data[lo], (size_t)chars->length)) lo++;
    while (hi > lo && memchr(chars->data, (unsigned char)s->data[hi - 1], (size_t)chars->length)) hi--;
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s->data + lo, hi - lo));
}

static MsValue str_trim_left(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* chars;
    if (!require_string(vm, argv[0], "strings.trim_left", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.trim_left", &chars)) return MS_NIL_VAL();
    int lo = 0;
    while (lo < s->length && memchr(chars->data, (unsigned char)s->data[lo], (size_t)chars->length)) lo++;
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s->data + lo, s->length - lo));
}

static MsValue str_trim_right(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s; MsObjString* chars;
    if (!require_string(vm, argv[0], "strings.trim_right", &s)) return MS_NIL_VAL();
    if (!require_string(vm, argv[1], "strings.trim_right", &chars)) return MS_NIL_VAL();
    int hi = s->length;
    while (hi > 0 && memchr(chars->data, (unsigned char)s->data[hi - 1], (size_t)chars->length)) hi--;
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s->data, hi));
}

static MsValue str_trim_space(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.trim_space", &s)) return MS_NIL_VAL();
    int lo = 0, hi = s->length;
    while (lo < hi && isspace((unsigned char)s->data[lo])) lo++;
    while (hi > lo && isspace((unsigned char)s->data[hi - 1])) hi--;
    return MS_OBJ_VAL(ms_obj_string_copy(vm, s->data + lo, hi - lo));
}

/* ------------------------------------------------------------------ */
/* pad_left / pad_right / center                                       */
/* ------------------------------------------------------------------ */

static MsValue str_pad_left(MsVM* vm, int argc, MsValue* argv) {
    MsObjString* s;
    ms_i64 width;
    if (!require_string(vm, argv[0], "strings.pad_left", &s)) return MS_NIL_VAL();
    if (!require_int(vm, argv[1], "strings.pad_left", &width)) return MS_NIL_VAL();
    char fill_ch = ' ';
    if (argc >= 3 && MS_IS_STRING(argv[2]) && MS_AS_STRING(argv[2])->length > 0)
        fill_ch = MS_AS_STRING(argv[2])->data[0];
    int pad = (int)width - s->length;
    if (pad <= 0) return MS_OBJ_VAL(ms_obj_string_copy(vm, s->data, s->length));
    char* buf = (char*)malloc((size_t)(pad + s->length) + 1);
    if (!buf) abort();
    memset(buf, fill_ch, (size_t)pad);
    memcpy(buf + pad, s->data, (size_t)s->length);
    buf[pad + s->length] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, pad + s->length));
}

static MsValue str_pad_right(MsVM* vm, int argc, MsValue* argv) {
    MsObjString* s;
    ms_i64 width;
    if (!require_string(vm, argv[0], "strings.pad_right", &s)) return MS_NIL_VAL();
    if (!require_int(vm, argv[1], "strings.pad_right", &width)) return MS_NIL_VAL();
    char fill_ch = ' ';
    if (argc >= 3 && MS_IS_STRING(argv[2]) && MS_AS_STRING(argv[2])->length > 0)
        fill_ch = MS_AS_STRING(argv[2])->data[0];
    int pad = (int)width - s->length;
    if (pad <= 0) return MS_OBJ_VAL(ms_obj_string_copy(vm, s->data, s->length));
    char* buf = (char*)malloc((size_t)(s->length + pad) + 1);
    if (!buf) abort();
    memcpy(buf, s->data, (size_t)s->length);
    memset(buf + s->length, fill_ch, (size_t)pad);
    buf[s->length + pad] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, s->length + pad));
}

static MsValue str_center(MsVM* vm, int argc, MsValue* argv) {
    MsObjString* s;
    ms_i64 width;
    if (!require_string(vm, argv[0], "strings.center", &s)) return MS_NIL_VAL();
    if (!require_int(vm, argv[1], "strings.center", &width)) return MS_NIL_VAL();
    char fill_ch = ' ';
    if (argc >= 3 && MS_IS_STRING(argv[2]) && MS_AS_STRING(argv[2])->length > 0)
        fill_ch = MS_AS_STRING(argv[2])->data[0];
    int total_pad = (int)width - s->length;
    if (total_pad <= 0) return MS_OBJ_VAL(ms_obj_string_copy(vm, s->data, s->length));
    int left_pad = total_pad / 2;
    int right_pad = total_pad - left_pad;
    int total = left_pad + s->length + right_pad;
    char* buf = (char*)malloc((size_t)total + 1);
    if (!buf) abort();
    memset(buf, fill_ch, (size_t)left_pad);
    memcpy(buf + left_pad, s->data, (size_t)s->length);
    memset(buf + left_pad + s->length, fill_ch, (size_t)right_pad);
    buf[total] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, total));
}

/* ------------------------------------------------------------------ */
/* to_upper / to_lower / title / capitalize                           */
/* ------------------------------------------------------------------ */

static MsValue str_to_upper(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.to_upper", &s)) return MS_NIL_VAL();
    char* buf = (char*)malloc((size_t)s->length + 1);
    if (!buf) abort();
    for (int i = 0; i < s->length; i++) buf[i] = (char)toupper((unsigned char)s->data[i]);
    buf[s->length] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, s->length));
}

static MsValue str_to_lower(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.to_lower", &s)) return MS_NIL_VAL();
    char* buf = (char*)malloc((size_t)s->length + 1);
    if (!buf) abort();
    for (int i = 0; i < s->length; i++) buf[i] = (char)tolower((unsigned char)s->data[i]);
    buf[s->length] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, s->length));
}

static MsValue str_title(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.title", &s)) return MS_NIL_VAL();
    char* buf = (char*)malloc((size_t)s->length + 1);
    if (!buf) abort();
    bool cap_next = true;
    for (int i = 0; i < s->length; i++) {
        unsigned char c = (unsigned char)s->data[i];
        if (isspace(c)) {
            buf[i] = (char)c;
            cap_next = true;
        } else if (cap_next) {
            buf[i] = (char)toupper(c);
            cap_next = false;
        } else {
            buf[i] = (char)tolower(c);
        }
    }
    buf[s->length] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, s->length));
}

static MsValue str_capitalize(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.capitalize", &s)) return MS_NIL_VAL();
    if (s->length == 0) return MS_OBJ_VAL(ms_obj_string_copy(vm, "", 0));
    char* buf = (char*)malloc((size_t)s->length + 1);
    if (!buf) abort();
    buf[0] = (char)toupper((unsigned char)s->data[0]);
    memcpy(buf + 1, s->data + 1, (size_t)(s->length - 1));
    buf[s->length] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, s->length));
}

/* ------------------------------------------------------------------ */
/* Char-class predicates  (accept single-char str)                    */
/* ------------------------------------------------------------------ */

static MsValue str_is_alpha(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.is_alpha", &s)) return MS_NIL_VAL();
    if (s->length == 0) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(isalpha((unsigned char)s->data[0]) != 0);
}

static MsValue str_is_digit(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.is_digit", &s)) return MS_NIL_VAL();
    if (s->length == 0) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(isdigit((unsigned char)s->data[0]) != 0);
}

static MsValue str_is_alnum(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.is_alnum", &s)) return MS_NIL_VAL();
    if (s->length == 0) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(isalnum((unsigned char)s->data[0]) != 0);
}

static MsValue str_is_space(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.is_space", &s)) return MS_NIL_VAL();
    if (s->length == 0) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(isspace((unsigned char)s->data[0]) != 0);
}

static MsValue str_is_upper(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.is_upper", &s)) return MS_NIL_VAL();
    if (s->length == 0) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(isupper((unsigned char)s->data[0]) != 0);
}

static MsValue str_is_lower(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.is_lower", &s)) return MS_NIL_VAL();
    if (s->length == 0) return MS_BOOL_VAL(false);
    return MS_BOOL_VAL(islower((unsigned char)s->data[0]) != 0);
}

/* ------------------------------------------------------------------ */
/* Byte encoding                                                        */
/* ------------------------------------------------------------------ */

static MsValue str_to_bytes(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.to_bytes", &s)) return MS_NIL_VAL();
    MsObjList* list = ms_obj_list_new(vm);
    for (int i = 0; i < s->length; i++) {
        ms_value_array_push(&list->items,
            MS_INT_VAL((ms_i64)(unsigned char)s->data[i]));
    }
    return MS_OBJ_VAL(list);
}

static MsValue str_from_bytes(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "strings.from_bytes: expected list");
        return MS_NIL_VAL();
    }
    MsObjList* list = MS_AS_LIST(argv[0]);
    char* buf = (char*)malloc((size_t)list->items.count + 1);
    if (!buf) abort();
    for (int i = 0; i < list->items.count; i++) {
        ms_i64 b = MS_IS_INT(list->items.data[i]) ? MS_AS_INT(list->items.data[i]) : 0;
        buf[i] = (char)(unsigned char)(b & 0xFF);
    }
    buf[list->items.count] = '\0';
    return MS_OBJ_VAL(ms_obj_string_take(vm, buf, list->items.count));
}

static MsValue str_to_buffer(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.to_buffer", &s)) return MS_NIL_VAL();
    MsObjBuffer* buf = ms_obj_buffer_from_bytes(vm,
        (const uint8_t*)s->data, (size_t)s->length);
    return MS_OBJ_VAL(buf);
}

static MsValue str_is_empty(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    MsObjString* s;
    if (!require_string(vm, argv[0], "strings.is_empty", &s)) return MS_NIL_VAL();
    return MS_BOOL_VAL(s->length == 0);
}

/* ------------------------------------------------------------------ */
/* Registration                                                         */
/* ------------------------------------------------------------------ */

static const MsNativeDef ms_strings_defs[] = {
    /* concat & split */
    {"join",         str_join,        2},
    {"repeat",       str_repeat,      2},
    {"fields",       str_fields,      1},
    {"split",        str_split,       2},
    {"split_n",      str_split_n,     3},
    /* search */
    {"contains",     str_contains,    2},
    {"has_prefix",   str_has_prefix,  2},
    {"has_suffix",   str_has_suffix,  2},
    {"index",        str_index,       2},
    {"last_index",   str_last_index,  2},
    {"count",        str_count,       2},
    /* replace */
    {"replace",      str_replace,    -1},
    {"replace_all",  str_replace_all, 3},
    /* trim */
    {"trim",         str_trim_chars,  2},
    {"trim_left",    str_trim_left,   2},
    {"trim_right",   str_trim_right,  2},
    {"trim_space",   str_trim_space,  1},
    /* pad */
    {"pad_left",     str_pad_left,   -1},
    {"pad_right",    str_pad_right,  -1},
    {"center",       str_center,     -1},
    /* case */
    {"to_upper",     str_to_upper,    1},
    {"to_lower",     str_to_lower,    1},
    {"title",        str_title,       1},
    {"capitalize",   str_capitalize,  1},
    /* char class */
    {"is_alpha",     str_is_alpha,    1},
    {"is_digit",     str_is_digit,    1},
    {"is_alnum",     str_is_alnum,    1},
    {"is_space",     str_is_space,    1},
    {"is_upper",     str_is_upper,    1},
    {"is_lower",     str_is_lower,    1},
    /* bytes */
    {"to_bytes",     str_to_bytes,    1},
    {"from_bytes",   str_from_bytes,  1},
    {"to_buffer",    str_to_buffer,   1},
    {"is_empty",     str_is_empty,    1},
    {NULL, NULL, 0}
};

void ms_module_strings_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_strings_defs);
}
