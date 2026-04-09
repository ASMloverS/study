#include "test_assert.h"
#include "ms/common.h"
#include "ms/consts.h"

int main(void) {
    TEST_ASSERT(sizeof(ms_u8) == 1);
    TEST_ASSERT(sizeof(ms_u32) == 4);
    TEST_ASSERT(sizeof(ms_i64) == 8);
    TEST_ASSERT(MS_STACK_MAX == 256);
    TEST_ASSERT(MS_FRAMES_MAX == 64);
    printf("test_smoke: all passed\n");
    return 0;
}
