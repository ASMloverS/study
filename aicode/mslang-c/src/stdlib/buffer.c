#include "ms/stdlib_register.h"
#include "ms/stdlib/objbuffer.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/memory.h"
#include <string.h>
#include <stdio.h>

static void buf_ensure(struct MsVM* vm, MsObjBuffer* b, size_t needed) {
    if (b->cap >= needed) return;
    size_t new_cap = b->cap < 8 ? 8 : b->cap;
    while (new_cap < needed) new_cap *= 2;
    b->data = (uint8_t*)ms_reallocate(vm, b->data, b->cap, new_cap);
    b->cap = new_cap;
}

MsObjBuffer* ms_obj_buffer_new(struct MsVM* vm, size_t initial_cap) {
    MsObjBuffer* b = MS_ALLOC_OBJ(vm, MS_OBJ_BUFFER, MsObjBuffer, 0);
    b->len  = 0;
    b->cap  = 0;
    b->data = NULL;
    if (initial_cap > 0) {
        b->data = (uint8_t*)ms_reallocate(vm, NULL, 0, initial_cap);
        b->cap  = initial_cap;
    }
    return b;
}

MsObjBuffer* ms_obj_buffer_from_bytes(struct MsVM* vm, const uint8_t* bytes, size_t len) {
    MsObjBuffer* b = ms_obj_buffer_new(vm, len == 0 ? 1 : len);
    if (len > 0) {
        memcpy(b->data, bytes, len);
        b->len = len;
    }
    return b;
}

bool ms_objbuffer_invoke(struct MsVM* vm, MsObjBuffer* b,
                         MsObjString* method, int argc, MsValue* argv,
                         MsValue* out) {
    (void)argc;
    const char* name = method->data;

    if (strcmp(name, "len") == 0) {
        *out = MS_INT_VAL((ms_i64)b->len);
        return true;
    }
    if (strcmp(name, "get") == 0) {
        if (argc < 1) return false;
        ms_i64 i = MS_AS_INT(argv[0]);
        if (i < 0 || (size_t)i >= b->len) { *out = MS_NIL_VAL(); return true; }
        *out = MS_INT_VAL((ms_i64)b->data[i]);
        return true;
    }
    if (strcmp(name, "set") == 0) {
        if (argc < 2) return false;
        ms_i64 i = MS_AS_INT(argv[0]);
        if (i < 0 || (size_t)i >= b->len) { *out = MS_NIL_VAL(); return true; }
        b->data[i] = (uint8_t)MS_AS_INT(argv[1]);
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "slice") == 0) {
        if (argc < 2) return false;
        ms_i64 s = MS_AS_INT(argv[0]);
        ms_i64 e = MS_AS_INT(argv[1]);
        if (s < 0) s = 0;
        if ((size_t)e > b->len) e = (ms_i64)b->len;
        if (s >= e) { *out = MS_OBJ_VAL(ms_obj_buffer_new(vm, 0)); return true; }
        *out = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, b->data + s, (size_t)(e - s)));
        return true;
    }
    if (strcmp(name, "append") == 0) {
        if (argc < 1) return false;
        if (MS_IS_BUFFER(argv[0])) {
            MsObjBuffer* src = MS_AS_BUFFER(argv[0]);
            buf_ensure(vm, b, b->len + src->len);
            memcpy(b->data + b->len, src->data, src->len);
            b->len += src->len;
        } else if (MS_IS_STRING(argv[0])) {
            MsObjString* s = MS_AS_STRING(argv[0]);
            buf_ensure(vm, b, b->len + (size_t)s->length);
            memcpy(b->data + b->len, s->data, (size_t)s->length);
            b->len += (size_t)s->length;
        }
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "concat") == 0) {
        if (argc < 1 || !MS_IS_BUFFER(argv[0])) return false;
        MsObjBuffer* src = MS_AS_BUFFER(argv[0]);
        MsObjBuffer* nb  = ms_obj_buffer_new(vm, b->len + src->len);
        if (b->len   > 0) memcpy(nb->data,         b->data,   b->len);
        if (src->len > 0) memcpy(nb->data + b->len, src->data, src->len);
        nb->len = b->len + src->len;
        *out = MS_OBJ_VAL(nb);
        return true;
    }
    if (strcmp(name, "to_str") == 0) {
        *out = MS_OBJ_VAL(ms_obj_string_copy(vm, (char*)b->data, (int)b->len));
        return true;
    }
    if (strcmp(name, "to_hex") == 0) {
        size_t hex_len = b->len * 2;
        char*  hex     = (char*)ms_reallocate(vm, NULL, 0, hex_len + 1);
        for (size_t i = 0; i < b->len; i++)
            snprintf(hex + i * 2, 3, "%02x", b->data[i]);
        hex[hex_len] = '\0';
        *out = MS_OBJ_VAL(ms_obj_string_take(vm, hex, (int)hex_len));
        return true;
    }
    if (strcmp(name, "find") == 0) {
        if (argc < 1 || !MS_IS_BUFFER(argv[0])) return false;
        MsObjBuffer* sub = MS_AS_BUFFER(argv[0]);
        if (sub->len == 0) { *out = MS_INT_VAL(0); return true; }
        if (sub->len > b->len) { *out = MS_INT_VAL(-1); return true; }
        for (size_t i = 0; i <= b->len - sub->len; i++) {
            if (memcmp(b->data + i, sub->data, sub->len) == 0) {
                *out = MS_INT_VAL((ms_i64)i);
                return true;
            }
        }
        *out = MS_INT_VAL(-1);
        return true;
    }
    if (strcmp(name, "equals") == 0) {
        if (argc < 1 || !MS_IS_BUFFER(argv[0])) { *out = MS_BOOL_VAL(false); return true; }
        MsObjBuffer* other = MS_AS_BUFFER(argv[0]);
        bool eq = (b->len == other->len) && (memcmp(b->data, other->data, b->len) == 0);
        *out = MS_BOOL_VAL(eq);
        return true;
    }
    if (strcmp(name, "copy") == 0) {
        *out = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, b->data, b->len));
        return true;
    }
    return false;
}

void ms_module_buffer_init(MsVM* vm, MsObjModule* mod) {
    (void)vm; (void)mod;
}
