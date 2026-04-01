#include "table.h"
#include "object.h"
#include "value.h"
#include "memory.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void test_table_init_free(void)
{
	MsTable table;
	ms_table_init(&table);
	assert(table.entries == NULL);
	assert(table.count == 0);
	assert(table.capacity == 0);

	ms_table_free(&table);
	assert(table.entries == NULL);
	assert(table.count == 0);
	assert(table.capacity == 0);
	printf("  test_table_init_free PASSED\n");
}

static void test_table_set_get(void)
{
	MsTable table;
	ms_table_init(&table);

	MsString *key = ms_string_copy("x", 1);
	MsValue val = ms_number_val(42.0);

	bool is_new = ms_table_set(&table, key, val);
	assert(is_new);
	assert(table.count == 1);

	MsValue out;
	bool found = ms_table_get(&table, key, &out);
	assert(found);
	assert(ms_as_number(out) == 42.0);

	ms_table_free(&table);
	ms_object_free((MsObject *)key);
	printf("  test_table_set_get PASSED\n");
}

static void test_table_overwrite(void)
{
	MsTable table;
	ms_table_init(&table);

	MsString *key = ms_string_copy("x", 1);

	bool is_new1 = ms_table_set(&table, key, ms_number_val(1.0));
	assert(is_new1 == true);

	bool is_new2 = ms_table_set(&table, key, ms_number_val(2.0));
	assert(is_new2 == false);
	assert(table.count == 1);

	MsValue out;
	bool found = ms_table_get(&table, key, &out);
	assert(found);
	assert(ms_as_number(out) == 2.0);

	ms_table_free(&table);
	ms_object_free((MsObject *)key);
	printf("  test_table_overwrite PASSED\n");
}

static void test_table_remove_tombstone(void)
{
	MsTable table;
	ms_table_init(&table);

	MsString *keyA = ms_string_copy("a", 1);
	MsString *keyB = ms_string_copy("b", 1);
	MsString *keyC = ms_string_copy("c", 1);

	ms_table_set(&table, keyA, ms_number_val(1.0));
	ms_table_set(&table, keyB, ms_number_val(2.0));
	ms_table_set(&table, keyC, ms_number_val(3.0));

	bool removed = ms_table_remove(&table, keyB);
	assert(removed);

	MsValue out;
	assert(!ms_table_get(&table, keyB, &out));

	assert(ms_table_get(&table, keyA, &out));
	assert(ms_as_number(out) == 1.0);
	assert(ms_table_get(&table, keyC, &out));
	assert(ms_as_number(out) == 3.0);

	MsString *keyD = ms_string_copy("d", 1);
	assert(!ms_table_remove(&table, keyD));

	ms_table_free(&table);
	ms_object_free((MsObject *)keyA);
	ms_object_free((MsObject *)keyB);
	ms_object_free((MsObject *)keyC);
	ms_object_free((MsObject *)keyD);
	printf("  test_table_remove_tombstone PASSED\n");
}

static void test_table_growth(void)
{
	MsTable table;
	ms_table_init(&table);

	MsString *keys[200];
	for (int i = 0; i < 200; i++) {
		char buf[16];
		int len = snprintf(buf, sizeof(buf), "key_%d", i);
		keys[i] = ms_string_copy(buf, len);
		ms_table_set(&table, keys[i], ms_number_val((double)i));
	}

	assert(table.count == 200);

	for (int i = 0; i < 200; i++) {
		MsValue out;
		bool found = ms_table_get(&table, keys[i], &out);
		assert(found);
		assert(ms_as_number(out) == (double)i);
	}

	ms_table_free(&table);
	for (int i = 0; i < 200; i++) {
		ms_object_free((MsObject *)keys[i]);
	}
	printf("  test_table_growth PASSED\n");
}

static void test_find_string(void)
{
	MsTable table;
	ms_table_init(&table);

	MsString *s1 = ms_string_copy("hello", 5);
	MsString *s2 = ms_string_copy("world", 5);
	ms_table_set(&table, s1, ms_number_val(1.0));
	ms_table_set(&table, s2, ms_number_val(2.0));

	uint32_t hash = ms_string_hash("hello", 5);
	MsString *found = ms_table_find_string(&table, "hello", 5, hash);
	assert(found == s1);

	MsString *not_found = ms_table_find_string(&table, "nope", 4,
						    ms_string_hash("nope", 4));
	assert(not_found == NULL);

	ms_table_free(&table);
	ms_object_free((MsObject *)s1);
	ms_object_free((MsObject *)s2);
	printf("  test_find_string PASSED\n");
}

static void test_add_all(void)
{
	MsTable src;
	MsTable dst;
	ms_table_init(&src);
	ms_table_init(&dst);

	MsString *k1 = ms_string_copy("a", 1);
	MsString *k2 = ms_string_copy("b", 1);
	ms_table_set(&src, k1, ms_number_val(10.0));
	ms_table_set(&src, k2, ms_number_val(20.0));

	ms_table_add_all(&src, &dst);
	assert(dst.count == 2);

	MsValue out;
	assert(ms_table_get(&dst, k1, &out));
	assert(ms_as_number(out) == 10.0);
	assert(ms_table_get(&dst, k2, &out));
	assert(ms_as_number(out) == 20.0);

	ms_table_free(&src);
	ms_table_free(&dst);
	ms_object_free((MsObject *)k1);
	ms_object_free((MsObject *)k2);
	printf("  test_add_all PASSED\n");
}

static void test_gc_helpers(void)
{
	MsTable table;
	ms_table_init(&table);

	MsString *k1 = ms_string_copy("keep", 4);
	MsString *k2 = ms_string_copy("remove", 6);
	k1->base.isMarked = true;
	k2->base.isMarked = false;

	ms_table_set(&table, k1, ms_number_val(1.0));
	ms_table_set(&table, k2, ms_number_val(2.0));

	ms_table_remove_white(&table);
	assert(table.count == 1);

	MsValue out;
	assert(ms_table_get(&table, k1, &out));
	assert(!ms_table_get(&table, k2, &out));

	ms_table_mark(&table);

	ms_table_free(&table);
	ms_object_free((MsObject *)k1);
	ms_object_free((MsObject *)k2);
	printf("  test_gc_helpers PASSED\n");
}

int main(void)
{
	printf("Running table tests...\n");
	test_table_init_free();
	test_table_set_get();
	test_table_overwrite();
	test_table_remove_tombstone();
	test_table_growth();
	test_find_string();
	test_add_all();
	test_gc_helpers();
	printf("All table tests passed.\n");
	return 0;
}
