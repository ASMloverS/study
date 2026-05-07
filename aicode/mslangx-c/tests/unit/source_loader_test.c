#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#include <io.h>
#define MS_TEST_MKDIR(path) _mkdir(path)
#define MS_TEST_PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MS_TEST_MKDIR(path) mkdir(path, 0777)
#define MS_TEST_PATH_SEP '/'
#endif

#include "ms/cache/cache_format.h"
#include "ms/cache/source_loader.h"
#include "ms/value.h"

#include "test_assert.h"

static int ensure_directory(const char *path) {
  char *scratch;
  size_t length;
  size_t i;

  TEST_ASSERT(path != NULL);
  length = strlen(path);
  scratch = (char *) malloc(length + 1u);
  TEST_ASSERT(scratch != NULL);
  memcpy(scratch, path, length + 1u);

  for (i = 1; scratch[i] != '\0'; ++i) {
    if (scratch[i] != '/' && scratch[i] != '\\') {
      continue;
    }
    scratch[i] = '\0';
    if (scratch[0] != '\0') {
      (void) MS_TEST_MKDIR(scratch);
    }
    scratch[i] = MS_TEST_PATH_SEP;
  }

  free(scratch);
  return 0;
}

static int write_text_file(const char *path, const char *text) {
  FILE *file;

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

static int read_binary_file(const char *path, uint8_t **out_data, size_t *out_length) {
  FILE *file;
  long size;
  uint8_t *data;

  TEST_ASSERT(path != NULL);
  TEST_ASSERT(out_data != NULL);
  TEST_ASSERT(out_length != NULL);

#if defined(_WIN32)
  TEST_ASSERT(fopen_s(&file, path, "rb") == 0);
#else
  file = fopen(path, "rb");
#endif
  TEST_ASSERT(file != NULL);
  TEST_ASSERT(fseek(file, 0, SEEK_END) == 0);
  size = ftell(file);
  TEST_ASSERT(size >= 0);
  TEST_ASSERT(fseek(file, 0, SEEK_SET) == 0);
  data = (uint8_t *) malloc((size_t) size);
  TEST_ASSERT(data != NULL || size == 0);
  if (size > 0) {
    TEST_ASSERT(fread(data, 1, (size_t) size, file) == (size_t) size);
  }
  TEST_ASSERT(fclose(file) == 0);
  *out_data = data;
  *out_length = (size_t) size;
  return 0;
}

static int write_binary_file(const char *path, const uint8_t *data, size_t length) {
  FILE *file;

  TEST_ASSERT(ensure_directory(path) == 0);
#if defined(_WIN32)
  TEST_ASSERT(fopen_s(&file, path, "wb") == 0);
#else
  file = fopen(path, "wb");
#endif
  TEST_ASSERT(file != NULL);
  TEST_ASSERT(fwrite(data, 1, length, file) == length);
  TEST_ASSERT(fclose(file) == 0);
  return 0;
}

static int file_exists(const char *path) {
#if defined(_WIN32)
  return _access(path, 0) == 0;
#else
  return access(path, F_OK) == 0;
#endif
}

static int compare_chunks(const MsChunk *left, const MsChunk *right) {
  size_t i;
  MsValue left_value;
  MsValue right_value;
  int left_line;
  int right_line;

  TEST_ASSERT(ms_chunk_code_count(left) == ms_chunk_code_count(right));
  TEST_ASSERT(ms_chunk_constant_count(left) == ms_chunk_constant_count(right));

  for (i = 0; i < ms_chunk_code_count(left); ++i) {
    TEST_ASSERT(ms_chunk_get_line(left, i, &left_line));
    TEST_ASSERT(ms_chunk_get_line(right, i, &right_line));
    TEST_ASSERT(left_line == right_line);
    TEST_ASSERT(left->code.data[i] == right->code.data[i]);
  }

  for (i = 0; i < ms_chunk_constant_count(left); ++i) {
    TEST_ASSERT(ms_chunk_get_constant(left, i, &left_value));
    TEST_ASSERT(ms_chunk_get_constant(right, i, &right_value));
    TEST_ASSERT(ms_value_equals(left_value, right_value));
  }

  return 0;
}

static int build_source_paths(char *source_path,
                              size_t source_path_length,
                              char *cache_path,
                              size_t cache_path_length,
                              const char *scenario) {
#if defined(_WIN32)
  TEST_ASSERT(snprintf(source_path,
                       source_path_length,
                       "source_loader_test_tmp%c%s%cscript.ms",
                       MS_TEST_PATH_SEP,
                       scenario,
                       MS_TEST_PATH_SEP) > 0);
  TEST_ASSERT(snprintf(cache_path,
                       cache_path_length,
                       "source_loader_test_tmp%c%s%c__mscache__%cscript.msc",
                       MS_TEST_PATH_SEP,
                       scenario,
                       MS_TEST_PATH_SEP,
                       MS_TEST_PATH_SEP) > 0);
#else
  TEST_ASSERT(snprintf(source_path,
                       source_path_length,
                       "source_loader_test_tmp/%s/script.ms",
                       scenario) > 0);
  TEST_ASSERT(snprintf(cache_path,
                       cache_path_length,
                       "source_loader_test_tmp/%s/__mscache__/script.msc",
                       scenario) > 0);
#endif
  return 0;
}

static int test_cache_write_and_hit(void) {
  MsSourceLoadOptions options;
  MsSourceLoadResult first;
  MsSourceLoadResult second;
  MsDiagnosticList diagnostics;
  char source_path[256];
  char cache_path[256];
  char *derived_path = NULL;
  MsSourceLoadStatus status;
  const char *source = "var answer = 42\nprint answer\n";

  TEST_ASSERT(build_source_paths(source_path, sizeof(source_path), cache_path,
                                 sizeof(cache_path), "hit") == 0);
  TEST_ASSERT(write_text_file(source_path, source) == 0);

  ms_source_load_options_init(&options);
  ms_source_load_result_init(&first);
  ms_source_load_result_init(&second);
  ms_diag_list_init(&diagnostics);

  status = ms_source_load_source(source_path, &options, &diagnostics, &first);
  TEST_ASSERT(status == MS_SOURCE_LOAD_STATUS_OK);
  TEST_ASSERT(first.loaded_from_cache == 0);
  TEST_ASSERT(first.source.display_path != NULL);
  TEST_ASSERT(strcmp(first.source.display_path, source_path) == 0);
  TEST_ASSERT(first.cache_path != NULL);
  TEST_ASSERT(strcmp(first.cache_path, cache_path) == 0);
  TEST_ASSERT(file_exists(cache_path));

  status = ms_source_load_source(source_path, &options, &diagnostics, &second);
  TEST_ASSERT(status == MS_SOURCE_LOAD_STATUS_OK);
  TEST_ASSERT(second.loaded_from_cache == 1);
  TEST_ASSERT(second.source.display_path != NULL);
  TEST_ASSERT(strcmp(second.source.display_path, source_path) == 0);
  TEST_ASSERT(second.cache_path != NULL);
  TEST_ASSERT(strcmp(second.cache_path, cache_path) == 0);
  TEST_ASSERT(compare_chunks(&first.chunk, &second.chunk) == 0);

  TEST_ASSERT(ms_cache_derive_path(source_path, &derived_path));
  TEST_ASSERT(strcmp(derived_path, cache_path) == 0);
  free(derived_path);

  ms_source_load_result_destroy(&first);
  ms_source_load_result_destroy(&second);
  ms_diag_list_destroy(&diagnostics);
  return 0;
}

static int test_corrupt_cache_falls_back_to_source(void) {
  MsSourceLoadOptions options;
  MsSourceLoadResult first;
  MsSourceLoadResult second;
  MsDiagnosticList diagnostics;
  uint8_t *data = NULL;
  size_t length = 0;
  char source_path[256];
  char cache_path[256];
  MsSourceLoadStatus status;
  const char *source = "var answer = 123\nprint answer\n";

  TEST_ASSERT(build_source_paths(source_path, sizeof(source_path), cache_path,
                                 sizeof(cache_path), "corrupt") == 0);
  TEST_ASSERT(write_text_file(source_path, source) == 0);

  ms_source_load_options_init(&options);
  ms_source_load_result_init(&first);
  ms_source_load_result_init(&second);
  ms_diag_list_init(&diagnostics);

  status = ms_source_load_source(source_path, &options, &diagnostics, &first);
  TEST_ASSERT(status == MS_SOURCE_LOAD_STATUS_OK);
  TEST_ASSERT(first.loaded_from_cache == 0);

  TEST_ASSERT(read_binary_file(cache_path, &data, &length) == 0);
  TEST_ASSERT(length > 0);
  data[0] = 'X';
  TEST_ASSERT(write_binary_file(cache_path, data, length) == 0);
  free(data);

  status = ms_source_load_source(source_path, &options, &diagnostics, &second);
  TEST_ASSERT(status == MS_SOURCE_LOAD_STATUS_OK);
  TEST_ASSERT(second.loaded_from_cache == 0);
  TEST_ASSERT(compare_chunks(&first.chunk, &second.chunk) == 0);

  ms_source_load_result_destroy(&first);
  ms_source_load_result_destroy(&second);
  ms_diag_list_destroy(&diagnostics);
  return 0;
}

static int test_cache_disabled_skips_write(void) {
  MsSourceLoadOptions options;
  MsSourceLoadResult result;
  MsDiagnosticList diagnostics;
  char source_path[256];
  char cache_path[256];
  const char *source = "var answer = 7\nprint answer\n";
  MsSourceLoadStatus status;

  TEST_ASSERT(build_source_paths(source_path, sizeof(source_path), cache_path,
                                 sizeof(cache_path), "disabled") == 0);
  TEST_ASSERT(write_text_file(source_path, source) == 0);

  ms_source_load_options_init(&options);
  options.cache_enabled = 0;
  ms_source_load_result_init(&result);
  ms_diag_list_init(&diagnostics);

  status = ms_source_load_source(source_path, &options, &diagnostics, &result);
  TEST_ASSERT(status == MS_SOURCE_LOAD_STATUS_OK);
  TEST_ASSERT(result.loaded_from_cache == 0);
  TEST_ASSERT(file_exists(cache_path) == 0);

  ms_source_load_result_destroy(&result);
  ms_diag_list_destroy(&diagnostics);
  return 0;
}

static int test_non_ms_path_rejected(void) {
  MsSourceLoadOptions options;
  MsSourceLoadResult result;
  MsDiagnosticList diagnostics;
  char source_path[256];
  const char *source = "print 1\n";
  MsSourceLoadStatus status;

  TEST_ASSERT(snprintf(source_path,
                       sizeof(source_path),
                       "source_loader_test_tmp%cinvalid%cscript.txt",
                       MS_TEST_PATH_SEP,
                       MS_TEST_PATH_SEP) > 0);
  TEST_ASSERT(write_text_file(source_path, source) == 0);

  ms_source_load_options_init(&options);
  ms_source_load_result_init(&result);
  ms_diag_list_init(&diagnostics);

  status = ms_source_load_source(source_path, &options, &diagnostics, &result);
  TEST_ASSERT(status == MS_SOURCE_LOAD_STATUS_INVALID_ARGUMENT);
  TEST_ASSERT(result.loaded_from_cache == 0);
  TEST_ASSERT(result.source.display_path == NULL);
  TEST_ASSERT(ms_chunk_code_count(&result.chunk) == 0);

  ms_source_load_result_destroy(&result);
  ms_diag_list_destroy(&diagnostics);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_cache_write_and_hit() == 0);
  TEST_ASSERT(test_corrupt_cache_falls_back_to_source() == 0);
  TEST_ASSERT(test_cache_disabled_skips_write() == 0);
  TEST_ASSERT(test_non_ms_path_rejected() == 0);
  return 0;
}
