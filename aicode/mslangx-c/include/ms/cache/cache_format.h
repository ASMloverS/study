#ifndef MSLANGC_CACHE_CACHE_FORMAT_H_
#define MSLANGC_CACHE_CACHE_FORMAT_H_

#include <stddef.h>
#include <stdint.h>

enum {
  MS_CACHE_MAGIC_SIZE = 8,
  MS_CACHE_FORMAT_VERSION = 1,
  MS_CACHE_ABI_VERSION = 1,
  MS_CACHE_U16_SIZE = 2,
  MS_CACHE_U32_SIZE = 4,
  MS_CACHE_U64_SIZE = 8,
  MS_CACHE_I64_SIZE = 8,
  MS_CACHE_DOUBLE_SIZE = 8
};

extern const uint8_t ms_cache_magic[MS_CACHE_MAGIC_SIZE];

typedef enum MsCacheEntryKind {
  MS_CACHE_ENTRY_KIND_SOURCE = 1,
  MS_CACHE_ENTRY_KIND_MODULE = 2
} MsCacheEntryKind;

typedef struct MsCacheSourceMetadata {
  const char *display_path;
  uint64_t byte_size;
  int64_t modification_time;
} MsCacheSourceMetadata;

int ms_cache_write_u16_le(uint8_t *buffer,
                          size_t buffer_length,
                          size_t offset,
                          uint16_t value);
int ms_cache_read_u16_le(const uint8_t *buffer,
                         size_t buffer_length,
                         size_t offset,
                         uint16_t *out_value);

int ms_cache_write_u32_le(uint8_t *buffer,
                          size_t buffer_length,
                          size_t offset,
                          uint32_t value);
int ms_cache_read_u32_le(const uint8_t *buffer,
                         size_t buffer_length,
                         size_t offset,
                         uint32_t *out_value);

int ms_cache_write_u64_le(uint8_t *buffer,
                          size_t buffer_length,
                          size_t offset,
                          uint64_t value);
int ms_cache_read_u64_le(const uint8_t *buffer,
                         size_t buffer_length,
                         size_t offset,
                         uint64_t *out_value);

int ms_cache_write_i64_le(uint8_t *buffer,
                          size_t buffer_length,
                          size_t offset,
                          int64_t value);
int ms_cache_read_i64_le(const uint8_t *buffer,
                         size_t buffer_length,
                         size_t offset,
                         int64_t *out_value);

int ms_cache_write_double_le(uint8_t *buffer,
                             size_t buffer_length,
                             size_t offset,
                             double value);
int ms_cache_read_double_le(const uint8_t *buffer,
                            size_t buffer_length,
                            size_t offset,
                            double *out_value);

int ms_cache_derive_path(const char *source_path, char **out_cache_path);

#endif
