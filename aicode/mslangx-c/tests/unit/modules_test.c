#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define MS_TEST_MKDIR(path) _mkdir(path)
#define MS_TEST_PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MS_TEST_MKDIR(path) mkdir(path, 0777)
#define MS_TEST_PATH_SEP '/'
#endif

#include "ms/buffer.h"
#include "ms/diag.h"
#include "ms/frontend/lowering.h"
#include "ms/runtime/chunk.h"
#include "ms/runtime/vm.h"
#include "ms/value.h"

#include "test_assert.h"

static int ensure_directory(const char* path) {
  char* scratch;
  size_t length;
  size_t i;

  TEST_ASSERT(path != NULL);
  length = strlen(path);
  scratch = (char*) malloc(length + 1);
  TEST_ASSERT(scratch != NULL);
  memcpy(scratch, path, length + 1);

  for (i = 1; scratch[i] != '\0'; ++i) {
    if (scratch[i] != '/' && scratch[i] != '\\') {
      continue;
    }
    scratch[i] = '\0';
    if (scratch[0] != '\0') {
      MS_TEST_MKDIR(scratch);
    }
    scratch[i] = MS_TEST_PATH_SEP;
  }
  free(scratch);
  return 0;
}

static int write_file(const char* path, const char* text) {
  FILE* file;

  TEST_ASSERT(ensure_directory(path) == 0);
#if defined(_WIN32)
  TEST_ASSERT(fopen_s(&file, path, "wb") == 0);
#else
  file = fopen(path, "wb");
#endif
  TEST_ASSERT(file != NULL);
  TEST_ASSERT(fwrite(text, 1, strlen(text), file) == strlen(text));
  TEST_ASSERT(fclose(file) == 0);
  return 0;
}

static char* canonicalize_path(const char* path) {
#if defined(_WIN32)
  return _fullpath(NULL, path, 0);
#else
  return realpath(path, NULL);
#endif
}

static int append_output(void* user_data, const char* text, size_t length) {
  MsBuffer* buffer = (MsBuffer*) user_data;

  return ms_buffer_append(buffer, text, length);
}

static int expect_output_equals(const MsBuffer* buffer, const char* expected) {
  size_t expected_length = strlen(expected);

  TEST_ASSERT(buffer->length == expected_length);
  TEST_ASSERT(memcmp(buffer->data, expected, expected_length) == 0);
  return 0;
}

static int test_module_name_mapping_and_search_root_precedence(void) {
  MsVM vm;
  char* relative_path = NULL;
  char root_a[256];
  char root_a_variant[320];
  char root_b[256];
  char module_a_path[320];
  char module_b_path[320];
  char* resolved_path = NULL;
  char* expected_path = NULL;

  ms_vm_init(&vm);

  TEST_ASSERT(ms_module_build_file_path("pkg.tool", &relative_path));
#if defined(_WIN32)
  TEST_ASSERT(strcmp(relative_path, "pkg\\tool.ms") == 0);
#else
  TEST_ASSERT(strcmp(relative_path, "pkg/tool.ms") == 0);
#endif

  snprintf(root_a, sizeof(root_a), "modules_test_tmp%croot_a", MS_TEST_PATH_SEP);
  snprintf(root_b, sizeof(root_b), "modules_test_tmp%croot_b", MS_TEST_PATH_SEP);
  snprintf(root_a_variant,
           sizeof(root_a_variant),
           "modules_test_tmp%croot_a%c..%croot_a",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  snprintf(module_a_path,
           sizeof(module_a_path),
           "%s%cpkg%ctool.ms",
           root_a,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  snprintf(module_b_path,
           sizeof(module_b_path),
           "%s%cpkg%ctool.ms",
           root_b,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);

  TEST_ASSERT(write_file(module_a_path, "print 1\n") == 0);
  TEST_ASSERT(write_file(module_b_path, "print 2\n") == 0);

  TEST_ASSERT(ms_vm_add_search_root(&vm, root_a_variant));
  TEST_ASSERT(ms_vm_add_search_root(&vm, root_b));
  TEST_ASSERT(ms_vm_resolve_module_path(&vm, "pkg.tool", &resolved_path));

  expected_path = canonicalize_path(module_a_path);
  TEST_ASSERT(expected_path != NULL);
  TEST_ASSERT(strcmp(resolved_path, expected_path) == 0);

  free(expected_path);
  free(resolved_path);
  free(relative_path);
  ms_vm_destroy(&vm);
  return 0;
}

static int test_module_cache_uses_canonical_keys(void) {
  MsVM vm;
  char root[256];
  char module_path[320];
  char module_variant[384];
  char* canonical_path = NULL;
  MsModule* first;
  MsModule* second;
  int inserted_new = 0;

  ms_vm_init(&vm);

  snprintf(root, sizeof(root), "modules_test_tmp%ccache_root", MS_TEST_PATH_SEP);
  snprintf(module_path,
           sizeof(module_path),
           "%s%cpkg%ctool.ms",
           root,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  snprintf(module_variant,
           sizeof(module_variant),
           "modules_test_tmp%ccache_root%c.%cpkg%ctool.ms",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);

  TEST_ASSERT(write_file(module_path, "print 3\n") == 0);

  canonical_path = canonicalize_path(module_path);
  TEST_ASSERT(canonical_path != NULL);

  first = ms_vm_get_or_create_module(&vm, module_variant, &inserted_new);
  TEST_ASSERT(first != NULL);
  TEST_ASSERT(inserted_new);
  TEST_ASSERT(first->state == MS_MODULE_STATE_UNSEEN);

  second = ms_vm_get_or_create_module(&vm, canonical_path, &inserted_new);
  TEST_ASSERT(second != NULL);
  TEST_ASSERT(!inserted_new);
  TEST_ASSERT(first == second);
  TEST_ASSERT(first->canonical_path != NULL);
  TEST_ASSERT(strcmp(first->canonical_path, canonical_path) == 0);

  free(canonical_path);
  ms_vm_destroy(&vm);
  return 0;
}

static int test_module_cache_entries_are_gc_roots(void) {
  MsVM vm;
  char module_path[320];
  MsModule* module;
  MsString* key;
  MsString* value;
  int inserted_new = 0;

  ms_vm_init(&vm);

  snprintf(module_path,
           sizeof(module_path),
           "modules_test_tmp%ccache_root%cimports%ccached.ms",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  TEST_ASSERT(write_file(module_path, "print 4\n") == 0);

  module = ms_vm_get_or_create_module(&vm, module_path, &inserted_new);
  TEST_ASSERT(module != NULL);
  TEST_ASSERT(inserted_new);

  key = ms_string_from_cstr("cached");
  value = ms_string_from_cstr("value");
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

static int test_module_import_uses_default_source_loader(void) {
  MsVM vm;
  MsModule module;
  MsChunk chunk;
  MsDiagnosticList diagnostics;
  MsBuffer output;
  char module_root[256];
  char module_path[320];
  const char* source =
      "import cache.counter as counter\n"
      "print counter.value\n";

  ms_vm_init(&vm);
  ms_module_init(&module, "<unit>");
  ms_chunk_init(&chunk);
  ms_diag_list_init(&diagnostics);
  ms_buffer_init(&output);

  snprintf(module_root,
           sizeof(module_root),
           "modules_test_tmp%cdefault_loader%cfixtures%cmodules",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  snprintf(module_path,
           sizeof(module_path),
           "%s%ccache%ccounter.ms",
           module_root,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);

  TEST_ASSERT(write_file(module_path,
                         "var value = \"module\"\n"
                         "\n"
                         "print \"counter loaded\"\n") == 0);
  TEST_ASSERT(ms_vm_add_search_root(&vm, module_root));
  TEST_ASSERT(ms_compile_source("<unit>", source, &chunk, &diagnostics) ==
              MS_COMPILE_RESULT_OK);
  TEST_ASSERT(ms_diag_list_count(&diagnostics) == 0);

  ms_vm_set_current_module(&vm, &module);
  ms_vm_set_write_callback(&vm, append_output, &output);
  TEST_ASSERT(ms_vm_run_chunk(&vm, &chunk) == MS_VM_RESULT_OK);
  TEST_ASSERT(expect_output_equals(&output, "counter loaded\nmodule\n") == 0);

  ms_buffer_destroy(&output);
  ms_module_destroy(&module);
  ms_vm_destroy(&vm);
  ms_diag_list_destroy(&diagnostics);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_module_state_transitions(void) {
  MsModule module;

  ms_module_init(&module, "module.test");
  TEST_ASSERT(module.state == MS_MODULE_STATE_UNSEEN);

  TEST_ASSERT(ms_module_transition_state(&module, MS_MODULE_STATE_INITIALIZING));
  TEST_ASSERT(module.state == MS_MODULE_STATE_INITIALIZING);
  TEST_ASSERT(ms_module_transition_state(&module, MS_MODULE_STATE_INITIALIZED));
  TEST_ASSERT(module.state == MS_MODULE_STATE_INITIALIZED);
  TEST_ASSERT(ms_module_transition_state(&module, MS_MODULE_STATE_FAILED));
  TEST_ASSERT(module.state == MS_MODULE_STATE_FAILED);
  TEST_ASSERT(!ms_module_transition_state(&module, MS_MODULE_STATE_INITIALIZING));

  ms_module_destroy(&module);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_module_name_mapping_and_search_root_precedence() == 0);
  TEST_ASSERT(test_module_cache_uses_canonical_keys() == 0);
  TEST_ASSERT(test_module_cache_entries_are_gc_roots() == 0);
  TEST_ASSERT(test_module_import_uses_default_source_loader() == 0);
  TEST_ASSERT(test_module_state_transitions() == 0);
  return 0;
}
