#include "test_assert.h"
#include "ms/vm.h"
#include <stdio.h>

static void test_gc_runs_without_crash(void) {
    MsVM vm;
    ms_vm_init(&vm);
    ms_vm_interpret(&vm,
        "var i = 0\n"
        "while (i < 500) {\n"
        "  var s = \"temp\" + \"garbage\"\n"
        "  i = i + 1\n"
        "}", "<test>");
    ms_vm_free(&vm);
}

static void test_gc_preserves_live_objects(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var kept = \"alive\"\n"
        "var i = 0\n"
        "while (i < 500) { var _ = \"trash\"\n i = i + 1 }\n"
        "print(kept)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_gc_runs_without_crash();
    test_gc_preserves_live_objects();
    printf("test_gc: all passed\n");
    return 0;
}
