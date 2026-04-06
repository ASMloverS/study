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

#include "ms/runtime/vm.h"

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
  TEST_ASSERT(test_module_state_transitions() == 0);
  return 0;
}
