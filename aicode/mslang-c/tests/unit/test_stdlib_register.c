#include "test_assert.h"
#include "ms/module.h"
#include "ms/vm.h"
#include <stdio.h>

/* Verify ms_stdlib_register_all registers all 10 builtin modules.
   ms_vm_init calls ms_stdlib_register_all, so after init each module
   must be findable via ms_vm_find_builtin_module. */

static void test_all_modules_registered(void) {
    MsVM vm;
    ms_vm_init(&vm);

    const char* names[] = {
        "math", "os", "time", "io", "buffer",
        "hash", "log", "net", "debug", "gc"
    };
    int n = (int)(sizeof(names) / sizeof(names[0]));
    for (int i = 0; i < n; i++) {
        MsBuiltinModuleInit fn = ms_vm_find_builtin_module(&vm, names[i]);
        TEST_ASSERT(fn != NULL);
    }

    ms_vm_free(&vm);
}

/* Verify each init stub does not crash when invoked. */
static void test_init_stubs_no_crash(void) {
    MsVM vm;
    ms_vm_init(&vm);

    const char* names[] = {
        "math", "os", "time", "io", "buffer",
        "hash", "log", "net", "debug", "gc"
    };
    int n = (int)(sizeof(names) / sizeof(names[0]));
    for (int i = 0; i < n; i++) {
        MsObjModule* mod = ms_module_load(&vm, names[i], NULL);
        TEST_ASSERT(mod != NULL);
    }

    ms_vm_free(&vm);
}

int main(void) {
    test_all_modules_registered();
    test_init_stubs_no_crash();
    printf("test_stdlib_register: all passed\n");
    return 0;
}
