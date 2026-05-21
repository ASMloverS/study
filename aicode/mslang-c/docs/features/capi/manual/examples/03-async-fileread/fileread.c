#include "ms/module.h"
#include <stdio.h>
#include <stdlib.h>

static const MsModuleApi* g_api;

/* fileread.read_sync(path) -> string
   Synchronously reads the entire file at 'path' and returns its contents.

   v1 note: MsModuleApi v1 does not expose make_future / future_resolve /
   future_reject or threadpool_submit.  A true async implementation requires
   MsModuleApi v2.  This function blocks the main thread while the file is
   read — acceptable for small files and examples, but not production async IO.

   What a v2 async version would look like (pseudocode):
     MsValue fut = api->make_future(vm);
     // pin fut as GC root, submit job to vm threadpool
     // api->threadpool_submit(vm, job);
     // worker: fread -> push done_queue -> wakeup main
     // main EventLoop: api->future_resolve(vm, fut, str_val)
     return fut;  // .ms side: result = await fileread.read_async(path)
*/
static MsValue fn_read_sync(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1)
        return g_api->raise(vm, "fileread.read_sync(path): expected 1 argument");
    if (!g_api->is_string(argv[0]))
        return g_api->raise(vm, "fileread.read_sync: path must be a string");

    /* val_to_cstring pointer is valid until the next allocating api->* call.
       We use it only for fopen (a plain C call, not an api call), so it is
       safe here.  make_string below is the first allocating api call. */
    const char* path = g_api->val_to_cstring(argv[0]);

    FILE* f = NULL;
#ifdef _MSC_VER
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f)
        return g_api->raise(vm, "fileread.read_sync: cannot open file");

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return g_api->raise(vm, "fileread.read_sync: out of memory");
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    /* make_string copies buf into a VM-managed ObjString; free buf after. */
    MsValue result = g_api->make_string(vm, buf, (int)n);
    free(buf);
    return result;
}

MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "fileread: requires MsModuleApi v1");
        return;
    }
    g_api = api;
    api->def_native(vm, mod, "read_sync", fn_read_sync, 1);
}
