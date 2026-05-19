#include "ms/module.h"
#include "ms/common.h"

static MsValue ext_hello(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    return MS_OBJ_VAL(ms_obj_string_copy(vm, "hello from ext", 14));
}

MS_EXPORT void ms_module_init(const MsModuleApi* api,
                               MsVM*             vm,
                               MsObjModule*      mod) {
    if (api->version < 1) return;
    api->def_native(vm, mod, "hello", ext_hello, 0);
}
