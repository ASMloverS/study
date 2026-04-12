#include "test_assert.h"
#include "ms/table.h"
#include <stdio.h>

// MockString mirrors MsObjString layout exactly (chars ptr + inline storage).
typedef struct {
    MsObject obj;
    uint32_t hash;
    int length;
    char* chars;
    char data[32];
} MockString;

static MockString make_mock(const char* s, uint32_t h) {
    MockString m = {0};
    m.obj.type = MS_OBJ_STRING;
    m.hash = h;
    m.length = (int)strlen(s);
#if defined(_MSC_VER)
    strncpy_s(m.data, sizeof(m.data), s, 31);
#else
    strncpy(m.data, s, 31);
#endif
    return m;
}

// Must be called after the MockString is in its final stack location.
#define MOCK_FIX(m) ((m).chars = (m).data)

static void test_set_get(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k1 = make_mock("x", 100); MOCK_FIX(k1);
    ms_table_set(&t, (MsObjString*)&k1, MS_INT_VAL(42));
    MsValue out;
    TEST_ASSERT(ms_table_get(&t, (MsObjString*)&k1, &out));
    TEST_ASSERT(MS_AS_INT(out) == 42);
    ms_table_free(&t);
}

static void test_delete(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k1 = make_mock("a", 200); MOCK_FIX(k1);
    ms_table_set(&t, (MsObjString*)&k1, MS_INT_VAL(1));
    TEST_ASSERT(ms_table_delete(&t, (MsObjString*)&k1));
    MsValue out;
    TEST_ASSERT(!ms_table_get(&t, (MsObjString*)&k1, &out));
    ms_table_free(&t);
}

static void test_grow(void) {
    MsTable t;
    ms_table_init(&t);
    MockString keys[20];
    for (int i = 0; i < 20; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "k%d", i);
        keys[i] = make_mock(buf, (uint32_t)(i * 7 + 1)); MOCK_FIX(keys[i]);
        ms_table_set(&t, (MsObjString*)&keys[i], MS_INT_VAL(i));
    }
    TEST_ASSERT(t.live_count == 20);
    TEST_ASSERT(t.capacity >= 20);
    MsValue out;
    TEST_ASSERT(ms_table_get(&t, (MsObjString*)&keys[15], &out));
    TEST_ASSERT(MS_AS_INT(out) == 15);
    ms_table_free(&t);
}

static void test_overwrite(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k = make_mock("z", 77); MOCK_FIX(k);
    bool is_new = ms_table_set(&t, (MsObjString*)&k, MS_INT_VAL(10));
    TEST_ASSERT(is_new);
    bool is_new2 = ms_table_set(&t, (MsObjString*)&k, MS_INT_VAL(20));
    TEST_ASSERT(!is_new2);
    TEST_ASSERT(t.live_count == 1);
    MsValue out;
    TEST_ASSERT(ms_table_get(&t, (MsObjString*)&k, &out));
    TEST_ASSERT(MS_AS_INT(out) == 20);
    ms_table_free(&t);
}

static void test_add_all(void) {
    MsTable src, dst;
    ms_table_init(&src);
    ms_table_init(&dst);
    MockString k1 = make_mock("a", 11); MOCK_FIX(k1);
    MockString k2 = make_mock("b", 22); MOCK_FIX(k2);
    ms_table_set(&src, (MsObjString*)&k1, MS_INT_VAL(1));
    ms_table_set(&src, (MsObjString*)&k2, MS_INT_VAL(2));
    ms_table_add_all(&dst, &src);
    MsValue out;
    TEST_ASSERT(ms_table_get(&dst, (MsObjString*)&k1, &out));
    TEST_ASSERT(MS_AS_INT(out) == 1);
    TEST_ASSERT(ms_table_get(&dst, (MsObjString*)&k2, &out));
    TEST_ASSERT(MS_AS_INT(out) == 2);
    ms_table_free(&src);
    ms_table_free(&dst);
}

static void test_find_string(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k = make_mock("hello", 999);
    MOCK_FIX(k);
    ms_table_set(&t, (MsObjString*)&k, MS_BOOL_VAL(true));
    MsObjString* found = ms_table_find_string(&t, "hello", 5, 999);
    TEST_ASSERT(found == (MsObjString*)&k);
    MsObjString* not_found = ms_table_find_string(&t, "world", 5, 888);
    TEST_ASSERT(not_found == NULL);
    ms_table_free(&t);
}

static void test_remove_white(void) {
    MsTable t;
    ms_table_init(&t);
    MockString k1 = make_mock("alive", 10); MOCK_FIX(k1);
    MockString k2 = make_mock("dead",  20); MOCK_FIX(k2);
    ms_table_set(&t, (MsObjString*)&k1, MS_INT_VAL(1));
    ms_table_set(&t, (MsObjString*)&k2, MS_INT_VAL(2));
    k1.obj.is_marked = true;
    k2.obj.is_marked = false;
    ms_table_remove_white(&t);
    MsValue out;
    TEST_ASSERT( ms_table_get(&t, (MsObjString*)&k1, &out));
    TEST_ASSERT(!ms_table_get(&t, (MsObjString*)&k2, &out));
    TEST_ASSERT(t.live_count == 1);
    ms_table_free(&t);
}

int main(void) {
    test_set_get();
    test_delete();
    test_grow();
    test_overwrite();
    test_add_all();
    test_find_string();
    test_remove_white();
    printf("test_table: all passed\n");
    return 0;
}
