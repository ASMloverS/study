#ifndef MSLANGC_CACHE_CACHE_FILE_H_
#define MSLANGC_CACHE_CACHE_FILE_H_

#include <stddef.h>
#include <stdint.h>

#include "ms/cache/cache_format.h"
#include "ms/runtime/chunk.h"

enum {
  MS_CACHE_FILE_MAGIC_OFFSET = 0,
  MS_CACHE_FILE_FORMAT_VERSION_OFFSET = 8,
  MS_CACHE_FILE_ABI_VERSION_OFFSET = 10,
  MS_CACHE_FILE_ENTRY_KIND_OFFSET = 12,
  MS_CACHE_FILE_RESERVED_OFFSET = 14,
  MS_CACHE_FILE_DISPLAY_PATH_LENGTH_OFFSET = 16,
  MS_CACHE_FILE_SOURCE_SIZE_OFFSET = 20,
  MS_CACHE_FILE_SOURCE_MTIME_OFFSET = 28,
  MS_CACHE_FILE_PAYLOAD_OFFSET_OFFSET = 36,
  MS_CACHE_FILE_PAYLOAD_LENGTH_OFFSET = 44,
  MS_CACHE_FILE_HEADER_SIZE = 52
};

typedef enum MsCacheFileStatus {
  MS_CACHE_FILE_STATUS_OK = 0,
  MS_CACHE_FILE_STATUS_NOT_FOUND,
  MS_CACHE_FILE_STATUS_IO_ERROR,
  MS_CACHE_FILE_STATUS_INVALID_ARGUMENT,
  MS_CACHE_FILE_STATUS_FORMAT_ERROR,
  MS_CACHE_FILE_STATUS_INCOMPATIBLE_VERSION,
  MS_CACHE_FILE_STATUS_STALE_SOURCE,
  MS_CACHE_FILE_STATUS_DIRECTORY_ERROR,
  MS_CACHE_FILE_STATUS_WRITE_ERROR
} MsCacheFileStatus;

typedef struct MsCacheFileReadInfo {
  char *display_path;
  MsCacheEntryKind entry_kind;
  MsCacheSourceMetadata source;
  uint64_t payload_offset;
  uint64_t payload_length;
} MsCacheFileReadInfo;

typedef struct MsCacheFileWriteInfo {
  MsCacheEntryKind entry_kind;
  MsCacheSourceMetadata source;
} MsCacheFileWriteInfo;

void ms_cache_file_read_info_init(MsCacheFileReadInfo *info);
void ms_cache_file_read_info_destroy(MsCacheFileReadInfo *info);

MsCacheFileStatus ms_cache_file_read(const char *cache_path,
                                     const MsCacheSourceMetadata *expected_source,
                                     MsCacheFileReadInfo *out_info,
                                     MsChunk *out_chunk);
MsCacheFileStatus ms_cache_file_write(const char *cache_path,
                                      const MsCacheFileWriteInfo *info,
                                      const MsChunk *chunk);

#endif
