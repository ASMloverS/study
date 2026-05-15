/* test_async_scaffold.c - tests for ASYNC-01 scaffold:
 *   1. TK_ASYNC, TK_AWAIT, TK_SPAWN are valid token types
 *   2. Scanner recognises async/await/spawn as keywords
 *   3. MS_OBJ_FUTURE is a valid MsObjectType constant
 */
#include "../../tests/test_assert.h"
#include "ms/token.h"
#include "ms/object.h"
#include "ms/scanner.h"

static void test_token_type_values_exist(void) {
    /* Enum constants must exist and be distinct */
    TEST_ASSERT(MS_TK_ASYNC != MS_TK_AWAIT);
    TEST_ASSERT(MS_TK_AWAIT != MS_TK_SPAWN);
    TEST_ASSERT(MS_TK_ASYNC != MS_TK_SPAWN);
    /* All are below MS_TK_COUNT */
    TEST_ASSERT(MS_TK_ASYNC < MS_TK_COUNT);
    TEST_ASSERT(MS_TK_AWAIT < MS_TK_COUNT);
    TEST_ASSERT(MS_TK_SPAWN < MS_TK_COUNT);
}

static void test_scanner_async_keyword(void) {
    MsScanner s;
    ms_scanner_init(&s, "async");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_ASYNC);
}

static void test_scanner_await_keyword(void) {
    MsScanner s;
    ms_scanner_init(&s, "await");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_AWAIT);
}

static void test_scanner_spawn_keyword(void) {
    MsScanner s;
    ms_scanner_init(&s, "spawn");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_SPAWN);
}

static void test_scanner_async_not_identifier(void) {
    MsScanner s;
    /* "asyncfoo" must still be an identifier, not TK_ASYNC */
    ms_scanner_init(&s, "asyncfoo");
    TEST_ASSERT_EQ(ms_scanner_next(&s).type, MS_TK_IDENTIFIER);
}

static void test_obj_future_type_exists(void) {
    /* MS_OBJ_FUTURE must be a valid MsObjectType */
    MsObjectType t = MS_OBJ_FUTURE;
    TEST_ASSERT(t >= 0);
    /* It must not collide with any other type */
    TEST_ASSERT(t != MS_OBJ_STRING);
    TEST_ASSERT(t != MS_OBJ_COROUTINE);
}

int main(void) {
    test_token_type_values_exist();
    test_scanner_async_keyword();
    test_scanner_await_keyword();
    test_scanner_spawn_keyword();
    test_scanner_async_not_identifier();
    test_obj_future_type_exists();
    return 0;
}
