#include <string.h>

#include "ms/runtime/vm.h"
#include "ms/string.h"

#include "test_assert.h"

static int test_cached_module_globals_keep_strings_marked(void) {
  MsVM vm;
  MsModule* module;
  MsString* key;
  MsString* value;
  int inserted_new = 0;

  ms_vm_init(&vm);

  module = ms_vm_get_or_create_module(&vm, "string_test_tmp/cache_root.ms", &inserted_new);
  TEST_ASSERT(module != NULL);
  TEST_ASSERT(inserted_new);

  key = ms_string_from_cstr("interned_key");
  value = ms_string_from_cstr("interned_value");
  TEST_ASSERT(key != NULL);
  TEST_ASSERT(value != NULL);
  TEST_ASSERT(ms_table_set(&module->globals,
                           key,
                           ms_value_object((MsObject*) value),
                           NULL));

  ms_vm_gc_collect(&vm);

  TEST_ASSERT(vm.module_cache.count == 1);
  TEST_ASSERT(ms_table_get(&module->globals, key, NULL, NULL));
  TEST_ASSERT(vm.gc.collection_count == 1);
  TEST_ASSERT(vm.gc.free_count == 0);

  ms_vm_destroy(&vm);
  ms_string_free(value);
  ms_string_free(key);
  return 0;
}

int main(void) {
  char bytes[] = "module";
  MsString *left;
  MsString *right;
  MsString *empty;

  left = ms_string_new(bytes, strlen(bytes));
  right = ms_string_from_cstr("module");
  empty = ms_string_from_cstr("");

  TEST_ASSERT(left != NULL);
  TEST_ASSERT(right != NULL);
  TEST_ASSERT(empty != NULL);

  bytes[0] = 'X';

  TEST_ASSERT(left->object.type == MS_OBJ_STRING);
  TEST_ASSERT(left->object.marked == 0);
  TEST_ASSERT(left->object.next == NULL);
  TEST_ASSERT(left->length == 6);
  TEST_ASSERT(left->hash == ms_string_hash_bytes("module", 6));
  TEST_ASSERT(strcmp(left->bytes, "module") == 0);
  TEST_ASSERT(left->bytes[left->length] == '\0');

  TEST_ASSERT(right->object.type == MS_OBJ_STRING);
  TEST_ASSERT(right->length == 6);
  TEST_ASSERT(strcmp(right->bytes, "module") == 0);

  TEST_ASSERT(left != right);
  TEST_ASSERT(ms_string_equals(left, right));
  TEST_ASSERT(!ms_string_equals(left, empty));

  TEST_ASSERT(empty->length == 0);
  TEST_ASSERT(strcmp(empty->bytes, "") == 0);
  TEST_ASSERT(empty->hash == ms_string_hash_bytes("", 0));
  TEST_ASSERT(test_cached_module_globals_keep_strings_marked() == 0);

  ms_string_free(left);
  ms_string_free(right);
  ms_string_free(empty);
  return 0;
}
