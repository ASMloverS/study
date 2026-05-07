#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define MS_TEST_MKDIR(path) _mkdir(path)
#define MS_TEST_PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MS_TEST_MKDIR(path) mkdir(path, 0777)
#define MS_TEST_PATH_SEP '/'
#endif

#include "ms/cache/cache_file.h"
#include "ms/runtime/function.h"
#include "ms/string.h"

#include "test_assert.h"

static int ensure_directory(const char *path) {
  char *scratch;
  size_t length;
  size_t i;

  TEST_ASSERT(path != NULL);
  length = strlen(path);
  scratch = (char *) malloc(length + 1);
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

static int write_binary_file(const char *path,
                             const uint8_t *data,
                             size_t length) {
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

static int read_binary_file(const char *path,
                            uint8_t **out_data,
                            size_t *out_length) {
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

static int write_test_chunk(MsChunk *chunk) {
  uint8_t index;
  MsString *string;

  ms_chunk_init(chunk);
  TEST_ASSERT(ms_chunk_write(chunk, 0x10, 1));
  TEST_ASSERT(ms_chunk_write(chunk, 0x20, 2));
  TEST_ASSERT(ms_chunk_write(chunk, 0x30, 3));
  TEST_ASSERT(ms_chunk_add_constant(chunk, ms_value_nil(), &index));
  TEST_ASSERT(ms_chunk_add_constant(chunk, ms_value_bool(1), &index));
  TEST_ASSERT(ms_chunk_add_constant(chunk, ms_value_number(42.5), &index));
  string = ms_string_from_cstr("cache-file");
  TEST_ASSERT(string != NULL);
  TEST_ASSERT(ms_chunk_add_constant(chunk,
                                    ms_value_object((MsObject *) string),
                                    &index));
  return 0;
}

static int compare_string_constant(const MsValue *left, const MsValue *right) {
  MsString *left_string;
  MsString *right_string;

  TEST_ASSERT(ms_value_is_string(*left));
  TEST_ASSERT(ms_value_is_string(*right));
  TEST_ASSERT(ms_value_get_string(*left, &left_string));
  TEST_ASSERT(ms_value_get_string(*right, &right_string));
  return ms_string_equals(left_string, right_string) ? 0 : 1;
}

static int compare_function_constant(const MsValue *left, const MsValue *right);

static int compare_chunk(const MsChunk *left, const MsChunk *right) {
  size_t i;
  MsValue left_value;
  MsValue right_value;
  int left_line;
  int right_line;

  TEST_ASSERT(ms_chunk_code_count(left) == ms_chunk_code_count(right));
  TEST_ASSERT(ms_chunk_constant_count(left) == ms_chunk_constant_count(right));

  for (i = 0; i < ms_chunk_code_count(left); ++i) {
    TEST_ASSERT(left->code.data[i] == right->code.data[i]);
    TEST_ASSERT(ms_chunk_get_line(left, i, &left_line));
    TEST_ASSERT(ms_chunk_get_line(right, i, &right_line));
    TEST_ASSERT(left_line == right_line);
  }

  for (i = 0; i < ms_chunk_constant_count(left); ++i) {
    TEST_ASSERT(ms_chunk_get_constant(left, i, &left_value));
    TEST_ASSERT(ms_chunk_get_constant(right, i, &right_value));
    TEST_ASSERT(left_value.type == right_value.type);
    if (ms_value_is_nil(left_value)) {
      continue;
    }
    if (ms_value_is_bool(left_value)) {
      int left_bool;
      int right_bool;

      TEST_ASSERT(ms_value_get_bool(left_value, &left_bool));
      TEST_ASSERT(ms_value_get_bool(right_value, &right_bool));
      TEST_ASSERT(left_bool == right_bool);
      continue;
    }
    if (ms_value_is_number(left_value)) {
      double left_number;
      double right_number;

      TEST_ASSERT(ms_value_get_number(left_value, &left_number));
      TEST_ASSERT(ms_value_get_number(right_value, &right_number));
      TEST_ASSERT(left_number == right_number);
      continue;
    }
    if (ms_value_is_string(left_value)) {
      TEST_ASSERT(compare_string_constant(&left_value, &right_value) == 0);
      continue;
    }
    if (ms_value_is_function(left_value)) {
      TEST_ASSERT(compare_function_constant(&left_value, &right_value) == 0);
      continue;
    }
    TEST_ASSERT(0);
  }

  return 0;
}

static int compare_function_constant(const MsValue *left, const MsValue *right) {
  MsFunction *left_function;
  MsFunction *right_function;
  const char *left_name;
  const char *right_name;

  TEST_ASSERT(ms_value_get_function(*left, &left_function));
  TEST_ASSERT(ms_value_get_function(*right, &right_function));
  TEST_ASSERT(left_function->arity == right_function->arity);
  TEST_ASSERT(left_function->upvalue_count == right_function->upvalue_count);
  TEST_ASSERT(left_function->flags == right_function->flags);

  left_name = left_function->name == NULL ? NULL : left_function->name->bytes;
  right_name = right_function->name == NULL ? NULL : right_function->name->bytes;
  if (left_name == NULL || right_name == NULL) {
    TEST_ASSERT(left_name == right_name);
  } else {
    TEST_ASSERT(strcmp(left_name, right_name) == 0);
  }

  return compare_chunk(&left_function->chunk, &right_function->chunk);
}

static int test_round_trip_read_write(void) {
  MsCacheFileWriteInfo write_info;
  MsCacheFileReadInfo read_info;
  MsChunk input_chunk;
  MsChunk output_chunk;
  char cache_path[256];
  char source_path[256];
  MsCacheFileStatus status;

  snprintf(source_path,
           sizeof(source_path),
           "cache_file_test_tmp%csource%cscript.ms",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  snprintf(cache_path,
           sizeof(cache_path),
           "cache_file_test_tmp%csource%c__mscache__%cscript.msc",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);

  TEST_ASSERT(write_text_file(source_path, "print 123\n") == 0);
  TEST_ASSERT(write_test_chunk(&input_chunk) == 0);

  write_info.entry_kind = MS_CACHE_ENTRY_KIND_SOURCE;
  write_info.source.display_path = source_path;
  write_info.source.byte_size = 10;
  write_info.source.modification_time = 12345;
  status = ms_cache_file_write(cache_path, &write_info, &input_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_OK);

  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&output_chunk);
  status = ms_cache_file_read(cache_path, &write_info.source, &read_info, &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_OK);
  TEST_ASSERT(read_info.entry_kind == MS_CACHE_ENTRY_KIND_SOURCE);
  TEST_ASSERT(read_info.source.byte_size == write_info.source.byte_size);
  TEST_ASSERT(read_info.source.modification_time ==
              write_info.source.modification_time);
  TEST_ASSERT(strcmp(read_info.display_path, source_path) == 0);
  TEST_ASSERT(compare_chunk(&input_chunk, &output_chunk) == 0);

  ms_cache_file_read_info_destroy(&read_info);
  ms_chunk_destroy(&input_chunk);
  ms_chunk_destroy(&output_chunk);
  return 0;
}

static int test_stale_metadata_rejected(void) {
  MsCacheFileWriteInfo write_info;
  MsCacheFileReadInfo read_info;
  MsChunk chunk;
  MsChunk output_chunk;
  char cache_path[256];
  char source_path[256];
  MsCacheFileStatus status;

  snprintf(source_path,
           sizeof(source_path),
           "cache_file_test_tmp%cstale%cscript.ms",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  snprintf(cache_path,
           sizeof(cache_path),
           "cache_file_test_tmp%cstale%c__mscache__%cscript.msc",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);

  TEST_ASSERT(write_test_chunk(&chunk) == 0);
  write_info.entry_kind = MS_CACHE_ENTRY_KIND_SOURCE;
  write_info.source.display_path = source_path;
  write_info.source.byte_size = 10;
  write_info.source.modification_time = 12345;
  TEST_ASSERT(ms_cache_file_write(cache_path, &write_info, &chunk) ==
              MS_CACHE_FILE_STATUS_OK);

  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&output_chunk);
  status = ms_cache_file_read(cache_path,
                              &(MsCacheSourceMetadata){source_path, 11, 12345},
                              &read_info,
                              &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_STALE_SOURCE);
  TEST_ASSERT(ms_chunk_code_count(&output_chunk) == 0);

  status = ms_cache_file_read(cache_path,
                              &(MsCacheSourceMetadata){source_path, 10, 12346},
                              &read_info,
                              &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_STALE_SOURCE);

  ms_cache_file_read_info_destroy(&read_info);
  ms_chunk_destroy(&chunk);
  ms_chunk_destroy(&output_chunk);
  return 0;
}

static int test_invalid_headers_rejected(void) {
  MsCacheFileWriteInfo write_info;
  MsChunk chunk;
  char cache_path[256];
  char source_path[256];
  uint8_t *data = NULL;
  uint8_t *baseline = NULL;
  size_t length = 0;
  size_t baseline_length = 0;
  MsCacheFileReadInfo read_info;
  MsChunk output_chunk;
  MsCacheFileStatus status;
  uint16_t version;

  snprintf(source_path,
           sizeof(source_path),
           "cache_file_test_tmp%cheader%cscript.ms",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  snprintf(cache_path,
           sizeof(cache_path),
           "cache_file_test_tmp%cheader%c__mscache__%cscript.msc",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);

  TEST_ASSERT(write_test_chunk(&chunk) == 0);
  write_info.entry_kind = MS_CACHE_ENTRY_KIND_SOURCE;
  write_info.source.display_path = source_path;
  write_info.source.byte_size = 10;
  write_info.source.modification_time = 12345;
  TEST_ASSERT(ms_cache_file_write(cache_path, &write_info, &chunk) ==
              MS_CACHE_FILE_STATUS_OK);

  TEST_ASSERT(read_binary_file(cache_path, &data, &length) == 0);
  baseline = (uint8_t *) malloc(length);
  TEST_ASSERT(baseline != NULL || length == 0);
  if (length > 0) {
    memcpy(baseline, data, length);
  }
  baseline_length = length;
  TEST_ASSERT(length > MS_CACHE_FILE_FORMAT_VERSION_OFFSET + 1);

  data[0] = 'X';
  TEST_ASSERT(write_binary_file(cache_path, data, length) == 0);
  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&output_chunk);
  status = ms_cache_file_read(cache_path, NULL, &read_info, &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_FORMAT_ERROR);
  ms_cache_file_read_info_destroy(&read_info);
  ms_chunk_destroy(&output_chunk);

  memcpy(data, baseline, baseline_length);
  TEST_ASSERT(write_binary_file(cache_path, data, baseline_length) == 0);
  version = 0;
  TEST_ASSERT(ms_cache_read_u16_le(data,
                                   baseline_length,
                                   MS_CACHE_FILE_FORMAT_VERSION_OFFSET,
                                   &version));
  version = (uint16_t) (version + 1u);
  TEST_ASSERT(ms_cache_write_u16_le(data,
                                    baseline_length,
                                    MS_CACHE_FILE_FORMAT_VERSION_OFFSET,
                                    version));
  TEST_ASSERT(write_binary_file(cache_path, data, baseline_length) == 0);
  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&output_chunk);
  status = ms_cache_file_read(cache_path, NULL, &read_info, &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_INCOMPATIBLE_VERSION);
  ms_cache_file_read_info_destroy(&read_info);
  ms_chunk_destroy(&output_chunk);

  memcpy(data, baseline, baseline_length);
  TEST_ASSERT(ms_cache_write_u16_le(data,
                                    baseline_length,
                                    MS_CACHE_FILE_FORMAT_VERSION_OFFSET,
                                    MS_CACHE_FORMAT_VERSION));
  TEST_ASSERT(ms_cache_write_u16_le(data,
                                    baseline_length,
                                    MS_CACHE_FILE_ABI_VERSION_OFFSET,
                                    (uint16_t) (MS_CACHE_ABI_VERSION + 1u)));
  TEST_ASSERT(write_binary_file(cache_path, data, baseline_length) == 0);
  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&output_chunk);
  status = ms_cache_file_read(cache_path, NULL, &read_info, &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_INCOMPATIBLE_VERSION);
  ms_cache_file_read_info_destroy(&read_info);
  ms_chunk_destroy(&output_chunk);

  memcpy(data, baseline, baseline_length);
  TEST_ASSERT(ms_cache_write_u64_le(data,
                                    baseline_length,
                                    MS_CACHE_FILE_PAYLOAD_OFFSET_OFFSET,
                                    (uint64_t) baseline_length + 1u));
  TEST_ASSERT(write_binary_file(cache_path, data, baseline_length) == 0);
  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&output_chunk);
  status = ms_cache_file_read(cache_path, NULL, &read_info, &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_FORMAT_ERROR);
  ms_cache_file_read_info_destroy(&read_info);
  ms_chunk_destroy(&output_chunk);

  memcpy(data, baseline, baseline_length);
  TEST_ASSERT(baseline_length > 0);
  TEST_ASSERT(write_binary_file(cache_path, data, baseline_length - 1u) == 0);
  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&output_chunk);
  status = ms_cache_file_read(cache_path, NULL, &read_info, &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_FORMAT_ERROR);
  ms_cache_file_read_info_destroy(&read_info);
  ms_chunk_destroy(&output_chunk);

  free(baseline);
  free(data);
  ms_chunk_destroy(&chunk);
  return 0;
}

static int test_atomic_replacement_overwrites_old_cache(void) {
  MsCacheFileWriteInfo write_info;
  MsChunk first_chunk;
  MsChunk second_chunk;
  MsChunk output_chunk;
  MsCacheFileReadInfo read_info;
  FILE *lock_file = NULL;
  char cache_path[256];
  char source_path[256];
  MsCacheFileStatus status;

  snprintf(source_path,
           sizeof(source_path),
           "cache_file_test_tmp%catomic%cscript.ms",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);
  snprintf(cache_path,
           sizeof(cache_path),
           "cache_file_test_tmp%catomic%c__mscache__%cscript.msc",
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP,
           MS_TEST_PATH_SEP);

  TEST_ASSERT(write_test_chunk(&first_chunk) == 0);
  TEST_ASSERT(write_test_chunk(&second_chunk) == 0);
  TEST_ASSERT(ms_chunk_patch_byte(&second_chunk, 0, 0x7f));

  write_info.entry_kind = MS_CACHE_ENTRY_KIND_SOURCE;
  write_info.source.display_path = source_path;
  write_info.source.byte_size = 10;
  write_info.source.modification_time = 1;
  TEST_ASSERT(ms_cache_file_write(cache_path, &write_info, &first_chunk) ==
              MS_CACHE_FILE_STATUS_OK);

#if defined(_WIN32)
  TEST_ASSERT(fopen_s(&lock_file, cache_path, "rb") == 0);
  TEST_ASSERT(lock_file != NULL);
#endif

  write_info.source.modification_time = 2;
  status = ms_cache_file_write(cache_path, &write_info, &second_chunk);
#if defined(_WIN32)
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_WRITE_ERROR);
#else
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_OK);
#endif

  ms_cache_file_read_info_init(&read_info);
  ms_chunk_init(&output_chunk);
  status = ms_cache_file_read(cache_path, NULL, &read_info, &output_chunk);
  TEST_ASSERT(status == MS_CACHE_FILE_STATUS_OK);
#if defined(_WIN32)
  TEST_ASSERT(compare_chunk(&first_chunk, &output_chunk) == 0);
#else
  TEST_ASSERT(compare_chunk(&second_chunk, &output_chunk) == 0);
#endif

  ms_cache_file_read_info_destroy(&read_info);
  if (lock_file != NULL) {
    fclose(lock_file);
  }
  ms_chunk_destroy(&first_chunk);
  ms_chunk_destroy(&second_chunk);
  ms_chunk_destroy(&output_chunk);
  return 0;
}

int main(void) {
  TEST_ASSERT(test_round_trip_read_write() == 0);
  TEST_ASSERT(test_stale_metadata_rejected() == 0);
  TEST_ASSERT(test_invalid_headers_rejected() == 0);
  TEST_ASSERT(test_atomic_replacement_overwrites_old_cache() == 0);
  return 0;
}
