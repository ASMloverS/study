#include "ms/module.h"
#include <stdio.h>

/* Store the api pointer so native functions can reach it. */
static const MsModuleApi* g_api;

/* hello(name) -> "Hello, <name>!" */
static MsValue hello_greet(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !g_api->is_string(argv[0])) {
        return g_api->raise(vm, "hello(name): expected string");
    }
    const char* name = g_api->val_to_cstring(argv[0]);
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "Hello, %s!", name);
    return g_api->make_string(vm, buf, len);
}

MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "hello: requires MsModuleApi v1");
        return;
    }
    g_api = api;

    api->def_native(vm, mod, "hello", hello_greet, 1);
    api->export_value(vm, mod, "VERSION", api->make_int(1));
}
