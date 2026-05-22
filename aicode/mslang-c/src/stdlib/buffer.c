#include "ms/stdlib_register.h"
#include "ms/stdlib/objbuffer.h"
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/memory.h"
#include <string.h>
#include <stdio.h>

/* ---- internal growth helper ---- */

static void buf_ensure(struct MsVM* vm, MsObjBuffer* b, size_t needed) {
    if (b->cap >= needed) return;
    size_t new_cap = b->cap < 8 ? 8 : b->cap;
    while (new_cap < needed) new_cap *= 2;
    b->data = (uint8_t*)ms_reallocate(vm, b->data, b->cap, new_cap);
    b->cap = new_cap;
}

/* ---- constructors ---- */

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
    MsObjBuffer* b = ms_obj_buffer_new(vm, len == 0 ? 0 : len);
    if (len > 0 && bytes) {
        memcpy(b->data, bytes, len);
        b->len = len;
    }
    return b;
}

/* ---- helper: resolve negative index ---- */
static bool buf_resolve_index(const MsObjBuffer* b, ms_i64 i, size_t* out) {
    if (i < 0) i += (ms_i64)b->len;
    if (i < 0 || (size_t)i >= b->len) return false;
    *out = (size_t)i;
    return true;
}

/* ---- helper: raw bytes from Buffer or String arg ---- */
static const uint8_t* arg_bytes(MsValue v, size_t* len_out) {
    if (MS_IS_BUFFER(v)) {
        MsObjBuffer* b = MS_AS_BUFFER(v);
        *len_out = b->len;
        return b->data;
    }
    if (MS_IS_STRING(v)) {
        MsObjString* s = MS_AS_STRING(v);
        *len_out = (size_t)s->length;
        return (const uint8_t*)s->data;
    }
    *len_out = 0;
    return NULL;
}

/* ---- method dispatch ---- */

bool ms_objbuffer_invoke(struct MsVM* vm, MsObjBuffer* b,
                         MsObjString* method, int argc, MsValue* argv,
                         MsValue* out) {
    const char* name = method->data;

    /* b.len() */
    if (strcmp(name, "len") == 0) {
        *out = MS_INT_VAL((ms_i64)b->len);
        return true;
    }
    /* b.cap() */
    if (strcmp(name, "cap") == 0) {
        *out = MS_INT_VAL((ms_i64)b->cap);
        return true;
    }
    /* b.get(i) -- negative index supported */
    if (strcmp(name, "get") == 0) {
        if (argc < 1) return false;
        size_t idx;
        if (!buf_resolve_index(b, MS_AS_INT(argv[0]), &idx)) {
            *out = MS_NIL_VAL();
            return true;
        }
        *out = MS_INT_VAL((ms_i64)b->data[idx]);
        return true;
    }
    /* b.set(i, v) -- negative index supported */
    if (strcmp(name, "set") == 0) {
        if (argc < 2) return false;
        size_t idx;
        if (!buf_resolve_index(b, MS_AS_INT(argv[0]), &idx)) {
            *out = MS_NIL_VAL();
            return true;
        }
        b->data[idx] = (uint8_t)(MS_AS_INT(argv[1]) & 0xFF);
        *out = MS_NIL_VAL();
        return true;
    }
    /* b.slice(start, end=-1) */
    if (strcmp(name, "slice") == 0) {
        if (argc < 1) return false;
        ms_i64 s = MS_AS_INT(argv[0]);
        ms_i64 e = (argc >= 2) ? MS_AS_INT(argv[1]) : -1;
        if (s < 0) s = 0;
        if (e < 0 || (size_t)e > b->len) e = (ms_i64)b->len;
        if (s >= e) {
            *out = MS_OBJ_VAL(ms_obj_buffer_new(vm, 0));
            return true;
        }
        *out = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, b->data + s, (size_t)(e - s)));
        return true;
    }
    /* b.append(x) -- Buffer or str */
    if (strcmp(name, "append") == 0) {
        if (argc < 1) return false;
        size_t src_len;
        const uint8_t* src = arg_bytes(argv[0], &src_len);
        if (!src && src_len == 0) {
            /* empty string is ok */
            if (MS_IS_STRING(argv[0])) { *out = MS_NIL_VAL(); return true; }
            return false;
        }
        if (!src) return false;
        buf_ensure(vm, b, b->len + src_len);
        memcpy(b->data + b->len, src, src_len);
        b->len += src_len;
        *out = MS_NIL_VAL();
        return true;
    }
    /* b.prepend(x) -- Buffer or str, shifts existing data right */
    if (strcmp(name, "prepend") == 0) {
        if (argc < 1) return false;
        size_t src_len;
        const uint8_t* src = arg_bytes(argv[0], &src_len);
        if (!src && !MS_IS_STRING(argv[0])) return false;
        if (src_len == 0) { *out = MS_NIL_VAL(); return true; }
        buf_ensure(vm, b, b->len + src_len);
        memmove(b->data + src_len, b->data, b->len);
        memcpy(b->data, src, src_len);
        b->len += src_len;
        *out = MS_NIL_VAL();
        return true;
    }
    /* b.fill(v, start=0, end=-1) */
    if (strcmp(name, "fill") == 0) {
        if (argc < 1) return false;
        uint8_t fill_byte = (uint8_t)(MS_AS_INT(argv[0]) & 0xFF);
        size_t s = (argc >= 2) ? (size_t)MS_AS_INT(argv[1]) : 0;
        size_t e = (argc >= 3 && MS_AS_INT(argv[2]) >= 0)
                   ? (size_t)MS_AS_INT(argv[2])
                   : b->len;
        if (s > b->len) s = b->len;
        if (e > b->len) e = b->len;
        if (s < e) memset(b->data + s, fill_byte, e - s);
        *out = MS_NIL_VAL();
        return true;
    }
    /* b.clear() */
    if (strcmp(name, "clear") == 0) {
        b->len = 0;
        *out = MS_NIL_VAL();
        return true;
    }
    /* b.resize(n, fill=0) */
    if (strcmp(name, "resize") == 0) {
        if (argc < 1) return false;
        size_t n = (size_t)MS_AS_INT(argv[0]);
        uint8_t fill_byte = (argc >= 2) ? (uint8_t)(MS_AS_INT(argv[1]) & 0xFF) : 0;
        if (n > b->len) {
            buf_ensure(vm, b, n);
            memset(b->data + b->len, fill_byte, n - b->len);
        }
        b->len = n;
        *out = MS_NIL_VAL();
        return true;
    }
    /* b.copy() */
    if (strcmp(name, "copy") == 0) {
        *out = MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, b->data, b->len));
        return true;
    }
    /* b.to_str() */
    if (strcmp(name, "to_str") == 0) {
        *out = MS_OBJ_VAL(ms_obj_string_copy(vm, (const char*)b->data, (int)b->len));
        return true;
    }
    /* b.to_hex() */
    if (strcmp(name, "to_hex") == 0) {
        size_t hex_len = b->len * 2;
        char*  hex     = (char*)ms_reallocate(vm, NULL, 0, hex_len + 1);
        for (size_t i = 0; i < b->len; i++)
            snprintf(hex + i * 2, 3, "%02x", b->data[i]);
        hex[hex_len] = '\0';
        *out = MS_OBJ_VAL(ms_obj_string_take(vm, hex, (int)hex_len));
        return true;
    }
    /* b.find(sub, start=0) -- sub is Buffer or str */
    if (strcmp(name, "find") == 0) {
        if (argc < 1) return false;
        size_t sub_len;
        const uint8_t* sub = arg_bytes(argv[0], &sub_len);
        if (!sub && !MS_IS_STRING(argv[0])) return false;
        size_t start = (argc >= 2) ? (size_t)MS_AS_INT(argv[1]) : 0;
        if (sub_len == 0) { *out = MS_INT_VAL((ms_i64)start); return true; }
        if (sub_len > b->len || start > b->len - sub_len) {
            *out = MS_INT_VAL(-1);
            return true;
        }
        for (size_t i = start; i <= b->len - sub_len; i++) {
            if (memcmp(b->data + i, sub, sub_len) == 0) {
                *out = MS_INT_VAL((ms_i64)i);
                return true;
            }
        }
        *out = MS_INT_VAL(-1);
        return true;
    }
    /* b.replace(old, new, count=-1) -- returns new Buffer */
    if (strcmp(name, "replace") == 0) {
        if (argc < 2) return false;
        if (!MS_IS_BUFFER(argv[0]) || !MS_IS_BUFFER(argv[1])) return false;
        MsObjBuffer* pat  = MS_AS_BUFFER(argv[0]);
        MsObjBuffer* repl = MS_AS_BUFFER(argv[1]);
        int max_count = (argc >= 3) ? (int)MS_AS_INT(argv[2]) : -1;

        MsObjBuffer* nb = ms_obj_buffer_new(vm, b->len);
        if (pat->len == 0) {
            /* no-op: return copy */
            if (b->len > 0) {
                buf_ensure(vm, nb, b->len);
                memcpy(nb->data, b->data, b->len);
                nb->len = b->len;
            }
            *out = MS_OBJ_VAL(nb);
            return true;
        }
        int count = 0;
        size_t i = 0;
        while (i <= b->len - pat->len) {
            if ((max_count < 0 || count < max_count) &&
                memcmp(b->data + i, pat->data, pat->len) == 0) {
                buf_ensure(vm, nb, nb->len + repl->len);
                if (repl->len > 0)
                    memcpy(nb->data + nb->len, repl->data, repl->len);
                nb->len += repl->len;
                i += pat->len;
                count++;
            } else {
                buf_ensure(vm, nb, nb->len + 1);
                nb->data[nb->len++] = b->data[i++];
            }
        }
        /* append tail */
        if (i < b->len) {
            size_t tail = b->len - i;
            buf_ensure(vm, nb, nb->len + tail);
            memcpy(nb->data + nb->len, b->data + i, tail);
            nb->len += tail;
        }
        *out = MS_OBJ_VAL(nb);
        return true;
    }
    /* b.equals(x) */
    if (strcmp(name, "equals") == 0) {
        if (argc < 1 || !MS_IS_BUFFER(argv[0])) {
            *out = MS_BOOL_VAL(false);
            return true;
        }
        MsObjBuffer* other = MS_AS_BUFFER(argv[0]);
        bool eq = (b->len == other->len) &&
                  (b->len == 0 || memcmp(b->data, other->data, b->len) == 0);
        *out = MS_BOOL_VAL(eq);
        return true;
    }
    /* b.concat(other) -- method form returns new Buffer */
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

    return false;
}

/* ---- module-level native functions ---- */

/* buffer.new([size [, fill]]) */
static MsValue ms_buf_new(MsVM* vm, int argc, MsValue* argv) {
    ms_i64 size = (argc >= 1) ? MS_AS_INT(argv[0]) : 0;
    ms_i64 fill = (argc >= 2) ? MS_AS_INT(argv[1]) : 0;
    if (size < 0) {
        ms_vm_runtime_error(vm, "buffer.new: size must be >= 0");
        return MS_NIL_VAL();
    }
    MsObjBuffer* b = ms_obj_buffer_new(vm, (size_t)size);
    if (size > 0) {
        memset(b->data, (int)(fill & 0xFF), (size_t)size);
        b->len = (size_t)size;
    }
    return MS_OBJ_VAL(b);
}

/* buffer.from_str(s) */
static MsValue ms_buf_from_str(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "buffer.from_str: expected string");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, (const uint8_t*)s->data, (size_t)s->length));
}

/* buffer.from_hex(hex) -- ignores spaces, errors on odd length after stripping */
static MsValue ms_buf_from_hex(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "buffer.from_hex: expected string");
        return MS_NIL_VAL();
    }
    MsObjString* s  = MS_AS_STRING(argv[0]);
    const char*  p  = s->data;
    int          in = s->length;

    /* count non-space hex digits */
    int hex_count = 0;
    for (int i = 0; i < in; i++) {
        if (p[i] != ' ' && p[i] != '\t') hex_count++;
    }
    if (hex_count % 2 != 0) {
        ms_vm_runtime_error(vm, "buffer.from_hex: odd number of hex digits");
        return MS_NIL_VAL();
    }

    size_t out_len = (size_t)(hex_count / 2);
    MsObjBuffer* b = ms_obj_buffer_new(vm, out_len == 0 ? 0 : out_len);
    if (out_len == 0) return MS_OBJ_VAL(b);

    size_t wi = 0;
    int nibble = 0, has_hi = 0;
    for (int i = 0; i < in; i++) {
        char c = p[i];
        if (c == ' ' || c == '\t') continue;
        int val;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else {
            ms_vm_runtime_error(vm, "buffer.from_hex: invalid hex character '%c'", c);
            return MS_NIL_VAL();
        }
        if (!has_hi) { nibble = val << 4; has_hi = 1; }
        else { b->data[wi++] = (uint8_t)(nibble | val); has_hi = 0; }
    }
    b->len = out_len;
    return MS_OBJ_VAL(b);
}

/* buffer.concat(a, b) -- static form */
static MsValue ms_buf_concat(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_BUFFER(argv[0]) || !MS_IS_BUFFER(argv[1])) {
        ms_vm_runtime_error(vm, "buffer.concat: expected two Buffers");
        return MS_NIL_VAL();
    }
    MsObjBuffer* a  = MS_AS_BUFFER(argv[0]);
    MsObjBuffer* bb = MS_AS_BUFFER(argv[1]);
    MsObjBuffer* nb = ms_obj_buffer_new(vm, a->len + bb->len);
    if (a->len  > 0) memcpy(nb->data,          a->data,   a->len);
    if (bb->len > 0) memcpy(nb->data + a->len,  bb->data,  bb->len);
    nb->len = a->len + bb->len;
    return MS_OBJ_VAL(nb);
}

/* ---- registration ---- */

static const MsNativeDef ms_buffer_defs[] = {
    {"new",      ms_buf_new,      -1},
    {"from_str", ms_buf_from_str,  1},
    {"from_hex", ms_buf_from_hex,  1},
    {"concat",   ms_buf_concat,    2},
    {NULL, NULL, 0}
};

void ms_module_buffer_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_buffer_defs);
}
