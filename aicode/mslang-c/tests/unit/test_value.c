#include "test_assert.h"
#include "ms/value.h"
#include <stdio.h>

static void test_nil(void) {
    MsValue v = MS_NIL_VAL();
    TEST_ASSERT(MS_IS_NIL(v));
    TEST_ASSERT(!ms_value_is_truthy(v));
}
static void test_bool(void) {
    TEST_ASSERT(ms_value_is_truthy(MS_BOOL_VAL(true)));
    TEST_ASSERT(!ms_value_is_truthy(MS_BOOL_VAL(false)));
}
static void test_number(void) {
    MsValue v = MS_NUMBER_VAL(3.14);
    TEST_ASSERT(MS_IS_NUMBER(v));
    TEST_ASSERT_EQ(MS_AS_NUMBER(v), 3.14);
    TEST_ASSERT(!ms_value_is_truthy(MS_NUMBER_VAL(0.0)));
    TEST_ASSERT(ms_value_is_truthy(MS_NUMBER_VAL(1.0)));
}
static void test_int(void) {
    MsValue v = MS_INT_VAL(42);
    TEST_ASSERT(MS_IS_INT(v));
    TEST_ASSERT(MS_AS_INT(v) == 42);
    TEST_ASSERT(!ms_value_is_truthy(MS_INT_VAL(0)));
}
static void test_equality(void) {
    TEST_ASSERT(ms_value_equals(MS_NIL_VAL(), MS_NIL_VAL()));
    TEST_ASSERT(ms_value_equals(MS_INT_VAL(5), MS_NUMBER_VAL(5.0)));
    TEST_ASSERT(!ms_value_equals(MS_INT_VAL(5), MS_INT_VAL(6)));
    TEST_ASSERT(!ms_value_equals(MS_NIL_VAL(), MS_BOOL_VAL(false)));
}
static void test_array(void) {
    MsValueArray arr;
    ms_value_array_init(&arr);
    ms_value_array_push(&arr, MS_INT_VAL(1));
    ms_value_array_push(&arr, MS_INT_VAL(2));
    TEST_ASSERT_EQ(arr.count, 2);
    TEST_ASSERT(MS_AS_INT(arr.data[0]) == 1);
    ms_value_array_free(&arr);
}

int main(void) {
    test_nil(); test_bool(); test_number();
    test_int(); test_equality(); test_array();
    printf("test_value: all passed\n");
    return 0;
}
