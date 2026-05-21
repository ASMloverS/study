#include "ms/module.h"

static const MsModuleApi* g_api;

/* Type tag: static lifetime string literal.
   userdata_is uses pointer equality (fast path) + strcmp (fallback). */
static const char* const kHashTag = "djb2.Hash";

/* Private payload stored in the Userdata's vm-allocated data buffer.
   The VM allocates sizeof(HashCtx) bytes via its own malloc; the GC frees
   that buffer automatically after calling finalize. Because HashCtx holds no
   external resources (no file handles, no separate heap allocations), we pass
   finalize=NULL and let the VM handle the buffer lifetime entirely. */
typedef struct {
    uint64_t state;
} HashCtx;

/* djb2.create() -> Userdata(djb2.Hash)
   Demonstrates: userdata_new, userdata_data */
static MsValue fn_create(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    /* userdata_new(vm, bytes, finalize, mark, type_tag)
       finalize=NULL: no external resources; VM frees ud->data after GC
       mark=NULL:     HashCtx holds no MsValue references */
    MsValue ud = g_api->userdata_new(vm, sizeof(HashCtx),
                                     NULL, NULL, kHashTag);
    HashCtx* ctx = (HashCtx*)g_api->userdata_data(ud);
    ctx->state = 5381; /* djb2 seed */
    return ud;
}

/* djb2.update(h, data) -> nil
   Demonstrates: is_userdata, userdata_data, is_string, val_to_cstring, string_len */
static MsValue fn_update(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2)
        return g_api->raise(vm, "djb2.update(h, data): expected 2 arguments");
    if (!g_api->is_userdata(argv[0], kHashTag))
        return g_api->raise(vm, "djb2.update: h must be a djb2.Hash handle");
    if (!g_api->is_string(argv[1]))
        return g_api->raise(vm, "djb2.update: data must be a string");

    HashCtx* ctx = (HashCtx*)g_api->userdata_data(argv[0]);
    const char* s = g_api->val_to_cstring(argv[1]);
    int len = g_api->string_len(argv[1]);
    for (int i = 0; i < len; i++)
        ctx->state = ctx->state * 33 ^ (unsigned char)s[i];
    return g_api->make_nil();
}

/* djb2.digest(h) -> int
   Demonstrates: userdata_data, make_int */
static MsValue fn_digest(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1)
        return g_api->raise(vm, "djb2.digest(h): expected 1 argument");
    if (!g_api->is_userdata(argv[0], kHashTag))
        return g_api->raise(vm, "djb2.digest: h must be a djb2.Hash handle");
    HashCtx* ctx = (HashCtx*)g_api->userdata_data(argv[0]);
    return g_api->make_int((int64_t)ctx->state);
}

/* djb2.reset(h) -> nil
   Demonstrates: userdata_data */
static MsValue fn_reset(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1)
        return g_api->raise(vm, "djb2.reset(h): expected 1 argument");
    if (!g_api->is_userdata(argv[0], kHashTag))
        return g_api->raise(vm, "djb2.reset: h must be a djb2.Hash handle");
    HashCtx* ctx = (HashCtx*)g_api->userdata_data(argv[0]);
    ctx->state = 5381;
    return g_api->make_nil();
}

MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "djb2: requires MsModuleApi v1");
        return;
    }
    g_api = api;
    api->def_native(vm, mod, "create", fn_create, 0);
    api->def_native(vm, mod, "update", fn_update, 2);
    api->def_native(vm, mod, "digest", fn_digest, 1);
    api->def_native(vm, mod, "reset",  fn_reset,  1);
}
