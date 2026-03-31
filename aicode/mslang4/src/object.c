#include "object.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

static MsString *ms_alloc_string(void)
{
	MsString *str = MS_ALLOCATE(MsString, 1);
	str->base.type = MS_OBJ_STRING;
	str->base.next = NULL;
	str->base.isMarked = false;
	return str;
}

uint32_t ms_string_hash(const char *key, int length)
{
	uint32_t hash = 2166136261u;
	for (int i = 0; i < length; i++) {
		hash ^= (uint8_t)key[i];
		hash *= 16777619u;
	}
	return hash;
}

MsString *ms_string_copy(const char *chars, int length)
{
	uint32_t hash = ms_string_hash(chars, length);
	MsString *str = ms_alloc_string();
	str->chars = MS_ALLOCATE(char, length + 1);
	memcpy(str->chars, chars, length);
	str->chars[length] = '\0';
	str->length = length;
	str->hash = hash;
	return str;
}

MsString *ms_string_take(char *chars, int length)
{
	uint32_t hash = ms_string_hash(chars, length);
	MsString *str = ms_alloc_string();
	str->chars = chars;
	str->length = length;
	str->hash = hash;
	return str;
}

MsString *ms_string_concat(MsString *a, MsString *b)
{
	int length = a->length + b->length;
	char *chars = MS_ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';
	return ms_string_take(chars, length);
}

void ms_object_free(MsObject *obj)
{
	switch (obj->type) {
	case MS_OBJ_STRING: {
		MsString *str = (MsString *)obj;
		MS_FREE(char, str->chars, str->length + 1);
		MS_FREE(MsString, str, 1);
		break;
	}
	default:
		break;
	}
}

void ms_object_print(MsValue value)
{
	MsObject *obj = ms_as_obj(value);
	switch (obj->type) {
	case MS_OBJ_STRING: {
		MsString *str = (MsString *)obj;
		printf("%s", str->chars);
		break;
	}
	default:
		printf("<object %d>", obj->type);
		break;
	}
}
