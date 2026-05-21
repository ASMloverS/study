#include "ms/module.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static const MsModuleApi* g_api;

/* bufops.to_upper(s) -> string
   Demonstrates: is_string, string_len, val_to_cstring, make_string
   Pattern: read length and pointer -> allocate private buf -> transform bytes
   -> make_string (first and only allocating api call). */
static MsValue fn_to_upper(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !g_api->is_string(argv[0]))
        return g_api->raise(vm, "to_upper(s): expected string");

    /* string_len returns byte count; val_to_cstring pointer is valid until
       the next allocating api->* call (make_string below). */
    int         len = g_api->string_len(argv[0]);
    const char* src = g_api->val_to_cstring(argv[0]);

    char* buf = (char*)malloc((size_t)len);
    if (!buf)
        return g_api->raise(vm, "to_upper: out of memory");

    for (int i = 0; i < len; i++)
        buf[i] = (char)toupper((unsigned char)src[i]);

    /* make_string copies buf into a VM-managed ObjString; src is invalid
       after this call but we no longer need it. */
    MsValue result = g_api->make_string(vm, buf, len);
    free(buf);
    return result;
}

/* bufops.hex_encode(s) -> string
   Each input byte becomes two lowercase hex digits.
   Demonstrates expansion: output length = 2 * input byte count. */
static MsValue fn_hex_encode(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !g_api->is_string(argv[0]))
        return g_api->raise(vm, "hex_encode(s): expected string");

    int         len = g_api->string_len(argv[0]);
    const char* src = g_api->val_to_cstring(argv[0]);

    int   out_len = len * 2;
    char* buf     = (char*)malloc((size_t)out_len + 1);
    if (!buf)
        return g_api->raise(vm, "hex_encode: out of memory");

    for (int i = 0; i < len; i++)
        snprintf(buf + i * 2, 3, "%02x", (unsigned char)src[i]);

    MsValue result = g_api->make_string(vm, buf, out_len);
    free(buf);
    return result;
}

/* bufops.byte_count(s) -> int
   Returns the byte length of s (not the Unicode code-point count).
   For ASCII strings these are identical; for UTF-8 strings they differ. */
static MsValue fn_byte_count(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !g_api->is_string(argv[0]))
        return g_api->raise(vm, "byte_count(s): expected string");
    return g_api->make_int(g_api->string_len(argv[0]));
}

MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "bufops: requires MsModuleApi v1");
        return;
    }
    g_api = api;
    api->def_native(vm, mod, "to_upper",   fn_to_upper,   1);
    api->def_native(vm, mod, "hex_encode", fn_hex_encode, 1);
    api->def_native(vm, mod, "byte_count", fn_byte_count, 1);
}
