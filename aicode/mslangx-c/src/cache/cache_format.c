#include "ms/cache/cache_format.h"

#include <stdlib.h>
#include <string.h>

const uint8_t ms_cache_magic[MS_CACHE_MAGIC_SIZE] = {
    'M', 'S', 'L', 'C', 'M', 'S', 'C', '\0'};

static int ms_cache_is_path_separator(char ch) {
  return ch == '/' || ch == '\\';
}

static char ms_cache_path_separator(void) {
#if defined(_WIN32)
  return '\\';
#else
  return '/';
#endif
}

static int ms_cache_check_bounds(size_t buffer_length,
                                 size_t offset,
                                 size_t value_size) {
  return offset <= buffer_length && value_size <= buffer_length - offset;
}

static int ms_cache_write_bytes(uint8_t *buffer,
                                size_t buffer_length,
                                size_t offset,
                                const uint8_t *bytes,
                                size_t value_size) {
  if (buffer == NULL || bytes == NULL ||
      !ms_cache_check_bounds(buffer_length, offset, value_size)) {
    return 0;
  }

  memcpy(buffer + offset, bytes, value_size);
  return 1;
}

static int ms_cache_read_bytes(const uint8_t *buffer,
                               size_t buffer_length,
                               size_t offset,
                               uint8_t *bytes,
                               size_t value_size) {
  if (buffer == NULL || bytes == NULL ||
      !ms_cache_check_bounds(buffer_length, offset, value_size)) {
    return 0;
  }

  memcpy(bytes, buffer + offset, value_size);
  return 1;
}

int ms_cache_write_u16_le(uint8_t *buffer,
                          size_t buffer_length,
                          size_t offset,
                          uint16_t value) {
  uint8_t bytes[MS_CACHE_U16_SIZE];

  bytes[0] = (uint8_t) (value & 0xffu);
  bytes[1] = (uint8_t) ((value >> 8) & 0xffu);
  return ms_cache_write_bytes(buffer, buffer_length, offset, bytes, sizeof(bytes));
}

int ms_cache_read_u16_le(const uint8_t *buffer,
                         size_t buffer_length,
                         size_t offset,
                         uint16_t *out_value) {
  uint8_t bytes[MS_CACHE_U16_SIZE];
  uint16_t value;

  if (out_value == NULL ||
      !ms_cache_read_bytes(buffer, buffer_length, offset, bytes, sizeof(bytes))) {
    return 0;
  }

  value = (uint16_t) bytes[0] | ((uint16_t) bytes[1] << 8);
  *out_value = value;
  return 1;
}

int ms_cache_write_u32_le(uint8_t *buffer,
                          size_t buffer_length,
                          size_t offset,
                          uint32_t value) {
  uint8_t bytes[MS_CACHE_U32_SIZE];

  bytes[0] = (uint8_t) (value & 0xffu);
  bytes[1] = (uint8_t) ((value >> 8) & 0xffu);
  bytes[2] = (uint8_t) ((value >> 16) & 0xffu);
  bytes[3] = (uint8_t) ((value >> 24) & 0xffu);
  return ms_cache_write_bytes(buffer, buffer_length, offset, bytes, sizeof(bytes));
}

int ms_cache_read_u32_le(const uint8_t *buffer,
                         size_t buffer_length,
                         size_t offset,
                         uint32_t *out_value) {
  uint8_t bytes[MS_CACHE_U32_SIZE];
  uint32_t value;

  if (out_value == NULL ||
      !ms_cache_read_bytes(buffer, buffer_length, offset, bytes, sizeof(bytes))) {
    return 0;
  }

  value = (uint32_t) bytes[0] |
          ((uint32_t) bytes[1] << 8) |
          ((uint32_t) bytes[2] << 16) |
          ((uint32_t) bytes[3] << 24);
  *out_value = value;
  return 1;
}

int ms_cache_write_u64_le(uint8_t *buffer,
                          size_t buffer_length,
                          size_t offset,
                          uint64_t value) {
  uint8_t bytes[MS_CACHE_U64_SIZE];
  size_t i;

  for (i = 0; i < sizeof(bytes); ++i) {
    bytes[i] = (uint8_t) ((value >> (8u * i)) & 0xffu);
  }
  return ms_cache_write_bytes(buffer, buffer_length, offset, bytes, sizeof(bytes));
}

int ms_cache_read_u64_le(const uint8_t *buffer,
                         size_t buffer_length,
                         size_t offset,
                         uint64_t *out_value) {
  uint8_t bytes[MS_CACHE_U64_SIZE];
  uint64_t value = 0;
  size_t i;

  if (out_value == NULL ||
      !ms_cache_read_bytes(buffer, buffer_length, offset, bytes, sizeof(bytes))) {
    return 0;
  }

  for (i = 0; i < sizeof(bytes); ++i) {
    value |= ((uint64_t) bytes[i]) << (8u * i);
  }
  *out_value = value;
  return 1;
}

int ms_cache_write_i64_le(uint8_t *buffer,
                          size_t buffer_length,
                          size_t offset,
                          int64_t value) {
  uint64_t encoded;

  memcpy(&encoded, &value, sizeof(encoded));
  return ms_cache_write_u64_le(buffer, buffer_length, offset, encoded);
}

int ms_cache_read_i64_le(const uint8_t *buffer,
                         size_t buffer_length,
                         size_t offset,
                         int64_t *out_value) {
  uint64_t encoded;

  if (out_value == NULL ||
      !ms_cache_read_u64_le(buffer, buffer_length, offset, &encoded)) {
    return 0;
  }

  memcpy(out_value, &encoded, sizeof(encoded));
  return 1;
}

int ms_cache_write_double_le(uint8_t *buffer,
                             size_t buffer_length,
                             size_t offset,
                             double value) {
  uint64_t bits;

  memcpy(&bits, &value, sizeof(bits));
  return ms_cache_write_u64_le(buffer, buffer_length, offset, bits);
}

int ms_cache_read_double_le(const uint8_t *buffer,
                            size_t buffer_length,
                            size_t offset,
                            double *out_value) {
  uint64_t bits;

  if (out_value == NULL ||
      !ms_cache_read_u64_le(buffer, buffer_length, offset, &bits)) {
    return 0;
  }

  memcpy(out_value, &bits, sizeof(bits));
  return 1;
}

static const char *ms_cache_find_last_separator(const char *path) {
  const char *cursor;
  const char *last_separator = NULL;

  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (ms_cache_is_path_separator(*cursor)) {
      last_separator = cursor;
    }
  }

  return last_separator;
}

static const char *ms_cache_find_extension(const char *path) {
  const char *cursor;
  const char *last_dot = NULL;

  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') {
      last_dot = cursor;
    }
    if (ms_cache_is_path_separator(*cursor)) {
      last_dot = NULL;
    }
  }

  return last_dot;
}

int ms_cache_derive_path(const char *source_path, char **out_cache_path) {
  const char *last_separator;
  const char *extension;
  const char *file_name;
  size_t directory_length;
  size_t root_length;
  size_t file_length;
  size_t base_length;
  size_t prefix_length;
  size_t total_length;
  char *cache_path;
  char separator;

  if (source_path == NULL || out_cache_path == NULL || source_path[0] == '\0') {
    return 0;
  }

  last_separator = ms_cache_find_last_separator(source_path);
  file_name = last_separator == NULL ? source_path : last_separator + 1;
  extension = ms_cache_find_extension(file_name);
  if (extension == NULL || strcmp(extension, ".ms") != 0) {
    return 0;
  }

  directory_length = last_separator == NULL ? 0u : (size_t) (last_separator - source_path);
  root_length = (last_separator != NULL && last_separator == source_path) ? 1u : directory_length;
  file_length = strlen(file_name);
  base_length = (size_t) (extension - file_name);
  prefix_length = root_length > 0 ? root_length + 1u + 11u + 1u : 11u + 1u;
  total_length = prefix_length + base_length + 4u + 1u;

  cache_path = (char *) malloc(total_length);
  if (cache_path == NULL) {
    return 0;
  }

  separator = ms_cache_path_separator();
  if (root_length > 0) {
    memcpy(cache_path, source_path, root_length);
    cache_path[root_length] = separator;
    memcpy(cache_path + root_length + 1, "__mscache__", 11);
    cache_path[root_length + 12] = separator;
    memcpy(cache_path + root_length + 13, file_name, base_length);
    memcpy(cache_path + root_length + 13 + base_length, ".msc", 4);
    cache_path[total_length - 1] = '\0';
  } else {
    memcpy(cache_path, "__mscache__", 11);
    cache_path[11] = separator;
    memcpy(cache_path + 12, file_name, base_length);
    memcpy(cache_path + 12 + base_length, ".msc", 4);
    cache_path[total_length - 1] = '\0';
  }

  (void) file_length;
  *out_cache_path = cache_path;
  return 1;
}
