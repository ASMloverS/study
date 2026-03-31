#include "object.h"
#include "value.h"
#include "memory.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void test_object_header(void)
{
	MsString *s = ms_string_copy("hello", 5);
	assert(s != NULL);
	assert(s->base.type == MS_OBJ_STRING);
	assert(s->base.next == NULL);
	assert(s->base.isMarked == false);
	assert(s->length == 5);
	assert(strncmp(s->chars, "hello", 5) == 0);
	assert(s->hash != 0);

	MsValue val = ms_obj_val((MsObject *)s);
	assert(MS_IS_STRING(val));
	assert(MS_AS_STRING(val) == s);

	ms_object_free((MsObject *)s);
	printf("  test_object_header PASSED\n");
}

static void test_string_hash(void)
{
	assert(ms_string_hash("", 0) == 2166136261u);

	uint32_t h1 = ms_string_hash("hello", 5);
	uint32_t h2 = ms_string_hash("hello", 5);
	assert(h1 == h2);

	uint32_t h3 = ms_string_hash("world", 5);
	assert(h1 != h3);

	MsString *s = ms_string_copy("hello", 5);
	assert(s->hash == h1);
	ms_object_free((MsObject *)s);

	printf("  test_string_hash PASSED\n");
}

static void test_string_take(void)
{
	char *buffer = MS_ALLOCATE(char, 6);
	memcpy(buffer, "hello", 5);
	buffer[5] = '\0';

	MsString *s = ms_string_take(buffer, 5);
	assert(s != NULL);
	assert(s->base.type == MS_OBJ_STRING);
	assert(s->length == 5);
	assert(strcmp(s->chars, "hello") == 0);
	assert(s->hash == ms_string_hash("hello", 5));
	assert(s->chars == buffer);

	ms_object_free((MsObject *)s);
	printf("  test_string_take PASSED\n");
}

static void test_string_concat(void)
{
	MsString *a = ms_string_copy("foo", 3);
	MsString *b = ms_string_copy("bar", 3);
	MsString *c = ms_string_concat(a, b);

	assert(c != NULL);
	assert(c->length == 6);
	assert(strcmp(c->chars, "foobar") == 0);
	assert(c->hash == ms_string_hash("foobar", 6));

	ms_object_free((MsObject *)c);
	ms_object_free((MsObject *)a);
	ms_object_free((MsObject *)b);
	printf("  test_string_concat PASSED\n");
}

static void test_object_print(void)
{
	MsString *s = ms_string_copy("hello world", 11);
	MsValue val = ms_obj_val((MsObject *)s);

	printf("  Expect 'hello world': ");
	ms_object_print(val);
	printf("\n");

	ms_object_free((MsObject *)s);
	printf("  test_object_print PASSED (visual check)\n");
}

static void test_object_free_and_consistency(void)
{
	for (int i = 0; i < 10; i++) {
		char buf[16];
		int len = snprintf(buf, sizeof(buf), "str_%d", i);
		MsString *s = ms_string_copy(buf, len);
		assert(s->hash == ms_string_hash(buf, len));
		ms_object_free((MsObject *)s);
	}

	MsString *s1 = ms_string_copy("abc", 3);
	MsString *s2 = ms_string_copy("abcd", 4);
	assert(s1->hash != s2->hash);
	ms_object_free((MsObject *)s1);
	ms_object_free((MsObject *)s2);

	printf("  test_object_free_and_consistency PASSED\n");
}

int main(void)
{
	printf("Running object string tests...\n");
	test_object_header();
	test_string_hash();
	test_string_take();
	test_string_concat();
	test_object_print();
	test_object_free_and_consistency();
	printf("All object string tests passed.\n");
	return 0;
}
