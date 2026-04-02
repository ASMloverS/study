#include <string.h>

#include "ms/object.h"
#include "ms/string.h"
#include "ms/table.h"
#include "ms/value.h"

#include "test_assert.h"

static int expect_formatted_value(MsValue value, const char *expected) {
  char buffer[64];

  TEST_ASSERT(ms_value_format(value, buffer, sizeof(buffer)));
  TEST_ASSERT(strcmp(buffer, expected) == 0);
  return 0;
}

static int expect_length(MsValue value, int expected_length) {
  int actual_length = -1;

  TEST_ASSERT(ms_value_length(value, &actual_length));
  TEST_ASSERT(actual_length == expected_length);
  return 0;
}

static int expect_length_failure(MsValue value) {
  int actual_length = -1;

  TEST_ASSERT(!ms_value_length(value, &actual_length));
  return 0;
}

int main(void) {
  MsList *list;
  MsTuple *tuple;
  MsMap *map;
  MsList *empty_list;
  MsTuple *empty_tuple;
  MsMap *empty_map;
  MsString *key;
  MsString *empty_string;
  MsList *out_list = NULL;
  MsTuple *out_tuple = NULL;
  MsMap *out_map = NULL;
  MsValue stored_value = ms_value_nil();
  MsValue list_value;
  MsValue tuple_value;
  MsValue map_value;
  int found = 0;
  int inserted_new = 0;

  list = ms_list_new();
  tuple = ms_tuple_new();
  map = ms_map_new();
  empty_list = ms_list_new();
  empty_tuple = ms_tuple_new();
  empty_map = ms_map_new();
  key = ms_string_from_cstr("answer");
  empty_string = ms_string_from_cstr("");

  TEST_ASSERT(list != NULL);
  TEST_ASSERT(tuple != NULL);
  TEST_ASSERT(map != NULL);
  TEST_ASSERT(empty_list != NULL);
  TEST_ASSERT(empty_tuple != NULL);
  TEST_ASSERT(empty_map != NULL);
  TEST_ASSERT(key != NULL);
  TEST_ASSERT(empty_string != NULL);

  TEST_ASSERT(list->object.type == MS_OBJ_LIST);
  TEST_ASSERT(tuple->object.type == MS_OBJ_TUPLE);
  TEST_ASSERT(map->object.type == MS_OBJ_MAP);

  TEST_ASSERT(list->elements.count == 0);
  TEST_ASSERT(list->elements.capacity == 0);
  TEST_ASSERT(list->elements.items == NULL);
  TEST_ASSERT(tuple->elements.count == 0);
  TEST_ASSERT(tuple->elements.capacity == 0);
  TEST_ASSERT(tuple->elements.items == NULL);
  TEST_ASSERT(map->entries != NULL);
  TEST_ASSERT(ms_table_count(map->entries) == 0);
  TEST_ASSERT(ms_table_capacity(map->entries) == 0);
  TEST_ASSERT(map->entries->entries == NULL);

  TEST_ASSERT(ms_value_array_append(&list->elements, ms_value_number(1.0)));
  TEST_ASSERT(ms_value_array_append(&list->elements, ms_value_bool(1)));
  TEST_ASSERT(ms_value_array_append(&tuple->elements, ms_value_number(2.0)));
  TEST_ASSERT(ms_value_array_append(&tuple->elements, ms_value_nil()));
  TEST_ASSERT(ms_table_set(map->entries,
                           key,
                           ms_value_number(42.0),
                           &inserted_new));
  TEST_ASSERT(inserted_new);
  TEST_ASSERT(ms_table_set(map->entries,
                           key,
                           ms_value_number(99.0),
                           &inserted_new));
  TEST_ASSERT(!inserted_new);

  TEST_ASSERT(list->elements.count == 2);
  TEST_ASSERT(list->elements.capacity >= list->elements.count);
  TEST_ASSERT(list->elements.items != NULL);
  TEST_ASSERT(ms_value_equals(list->elements.items[0], ms_value_number(1.0)));
  TEST_ASSERT(ms_value_equals(list->elements.items[1], ms_value_bool(1)));

  TEST_ASSERT(tuple->elements.count == 2);
  TEST_ASSERT(tuple->elements.capacity >= tuple->elements.count);
  TEST_ASSERT(tuple->elements.items != NULL);
  TEST_ASSERT(ms_value_equals(tuple->elements.items[0], ms_value_number(2.0)));
  TEST_ASSERT(ms_value_equals(tuple->elements.items[1], ms_value_nil()));

  TEST_ASSERT(ms_table_get(map->entries, key, &stored_value, &found));
  TEST_ASSERT(found);
  TEST_ASSERT(ms_value_equals(stored_value, ms_value_number(99.0)));

  list_value = ms_value_object((MsObject *) list);
  tuple_value = ms_value_object((MsObject *) tuple);
  map_value = ms_value_object((MsObject *) map);

  TEST_ASSERT(ms_value_is_list(list_value));
  TEST_ASSERT(ms_value_get_list(list_value, &out_list));
  TEST_ASSERT(out_list == list);
  TEST_ASSERT(ms_value_is_tuple(tuple_value));
  TEST_ASSERT(ms_value_get_tuple(tuple_value, &out_tuple));
  TEST_ASSERT(out_tuple == tuple);
  TEST_ASSERT(ms_value_is_map(map_value));
  TEST_ASSERT(ms_value_get_map(map_value, &out_map));
  TEST_ASSERT(out_map == map);

  TEST_ASSERT(expect_formatted_value(list_value, "<list>") == 0);
  TEST_ASSERT(expect_formatted_value(tuple_value, "<tuple>") == 0);
  TEST_ASSERT(expect_formatted_value(map_value, "<map>") == 0);

  TEST_ASSERT(ms_value_is_falsey(ms_value_object((MsObject *) empty_string)));
  TEST_ASSERT(ms_value_is_falsey(ms_value_object((MsObject *) empty_list)));
  TEST_ASSERT(ms_value_is_falsey(ms_value_object((MsObject *) empty_tuple)));
  TEST_ASSERT(ms_value_is_falsey(ms_value_object((MsObject *) empty_map)));
  TEST_ASSERT(!ms_value_is_falsey(list_value));
  TEST_ASSERT(!ms_value_is_falsey(tuple_value));
  TEST_ASSERT(!ms_value_is_falsey(map_value));

  TEST_ASSERT(expect_length(ms_value_object((MsObject *) key), 6) == 0);
  TEST_ASSERT(expect_length(list_value, 2) == 0);
  TEST_ASSERT(expect_length(tuple_value, 2) == 0);
  TEST_ASSERT(expect_length(map_value, 1) == 0);
  TEST_ASSERT(expect_length_failure(ms_value_nil()) == 0);
  TEST_ASSERT(expect_length_failure(ms_value_bool(1)) == 0);
  TEST_ASSERT(expect_length_failure(ms_value_number(3.0)) == 0);

  ms_map_free(map);
  ms_tuple_free(tuple);
  ms_list_free(list);
  ms_map_free(empty_map);
  ms_tuple_free(empty_tuple);
  ms_list_free(empty_list);
  ms_string_free(empty_string);
  ms_string_free(key);
  return 0;
}
