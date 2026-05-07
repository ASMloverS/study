#include "ms/cache/cache_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "ms/cache/chunk_codec.h"

static int ms_cache_file_is_path_separator(char ch) {
  return ch == '/' || ch == '\\';
}

static char ms_cache_file_path_separator(void) {
#if defined(_WIN32)
  return '\\';
#else
  return '/';
#endif
}

static int ms_cache_file_directory_exists(const char *path) {
#if defined(_WIN32)
  struct _stat info;

  if (path == NULL) {
    return 0;
  }
  return _stat(path, &info) == 0 && (info.st_mode & _S_IFDIR) != 0;
#else
  struct stat info;

  if (path == NULL) {
    return 0;
  }
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int ms_cache_file_make_directory(const char *path) {
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
  if (ms_cache_file_directory_exists(path)) {
    return 1;
  }

#if defined(_WIN32)
  if (_mkdir(path) == 0) {
    return 1;
  }
#else
  if (mkdir(path, 0777) == 0) {
    return 1;
  }
#endif

  return errno == EEXIST && ms_cache_file_directory_exists(path);
}

static int ms_cache_file_create_directories(const char *path) {
  char *scratch;
  size_t length;
  size_t i;

  if (path == NULL || path[0] == '\0') {
    return 0;
  }

  length = strlen(path);
  scratch = (char *) malloc(length + 1);
  if (scratch == NULL) {
    return 0;
  }
  memcpy(scratch, path, length + 1);

  for (i = 1; scratch[i] != '\0'; ++i) {
    if (!ms_cache_file_is_path_separator(scratch[i])) {
      continue;
    }
    scratch[i] = '\0';
    if (scratch[0] != '\0' && !(i == 2 && scratch[1] == ':')) {
      if (!ms_cache_file_make_directory(scratch)) {
        free(scratch);
        return 0;
      }
    }
    scratch[i] = ms_cache_file_path_separator();
  }

  free(scratch);
  return 1;
}

static int ms_cache_file_read_all(const char *path,
                                  uint8_t **out_buffer,
                                  size_t *out_length) {
  FILE *file;
  long size;
  size_t read_size;
  uint8_t *buffer;

  if (path == NULL || out_buffer == NULL || out_length == NULL) {
    return 0;
  }

#if defined(_WIN32)
  if (fopen_s(&file, path, "rb") != 0) {
    file = NULL;
  }
#else
  file = fopen(path, "rb");
#endif
  if (file == NULL) {
    return 0;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return 0;
  }

  size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }

  buffer = (uint8_t *) malloc((size_t) size);
  if (buffer == NULL && size > 0) {
    fclose(file);
    return 0;
  }

  read_size = size == 0 ? 0u : fread(buffer, 1, (size_t) size, file);
  fclose(file);
  if (read_size != (size_t) size) {
    free(buffer);
    return 0;
  }

  *out_buffer = buffer;
  *out_length = (size_t) size;
  return 1;
}

void ms_cache_file_read_info_init(MsCacheFileReadInfo *info) {
  if (info == NULL) {
    return;
  }

  info->display_path = NULL;
  info->entry_kind = MS_CACHE_ENTRY_KIND_SOURCE;
  info->source.display_path = NULL;
  info->source.byte_size = 0;
  info->source.modification_time = 0;
  info->payload_offset = 0;
  info->payload_length = 0;
}

void ms_cache_file_read_info_destroy(MsCacheFileReadInfo *info) {
  if (info == NULL) {
    return;
  }

  free(info->display_path);
  ms_cache_file_read_info_init(info);
}

static MsCacheFileStatus ms_cache_file_validate_header(const uint8_t *buffer,
                                                       size_t buffer_length,
                                                       MsCacheFileReadInfo *out_info,
                                                       size_t *out_payload_offset) {
  uint16_t format_version;
  uint16_t abi_version;
  uint16_t entry_kind;
  uint16_t reserved;
  uint32_t display_path_length;
  uint64_t source_size;
  int64_t source_mtime;
  uint64_t payload_offset;
  uint64_t payload_length;
  char *display_path;
  size_t header_size;

  if (buffer == NULL || out_info == NULL || out_payload_offset == NULL) {
    return MS_CACHE_FILE_STATUS_INVALID_ARGUMENT;
  }

  if (buffer_length < MS_CACHE_FILE_HEADER_SIZE) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }
  if (memcmp(buffer + MS_CACHE_FILE_MAGIC_OFFSET,
             ms_cache_magic,
             MS_CACHE_MAGIC_SIZE) != 0) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }
  if (!ms_cache_read_u16_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_FORMAT_VERSION_OFFSET,
                            &format_version) ||
      !ms_cache_read_u16_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_ABI_VERSION_OFFSET,
                            &abi_version) ||
      !ms_cache_read_u16_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_ENTRY_KIND_OFFSET,
                            &entry_kind) ||
      !ms_cache_read_u16_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_RESERVED_OFFSET,
                            &reserved) ||
      !ms_cache_read_u32_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_DISPLAY_PATH_LENGTH_OFFSET,
                            &display_path_length) ||
      !ms_cache_read_u64_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_SOURCE_SIZE_OFFSET,
                            &source_size) ||
      !ms_cache_read_i64_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_SOURCE_MTIME_OFFSET,
                            &source_mtime) ||
      !ms_cache_read_u64_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_PAYLOAD_OFFSET_OFFSET,
                            &payload_offset) ||
      !ms_cache_read_u64_le(buffer,
                            buffer_length,
                            MS_CACHE_FILE_PAYLOAD_LENGTH_OFFSET,
                            &payload_length)) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }

  if (format_version != MS_CACHE_FORMAT_VERSION ||
      abi_version != MS_CACHE_ABI_VERSION) {
    return MS_CACHE_FILE_STATUS_INCOMPATIBLE_VERSION;
  }
  if (reserved != 0) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }
  if (entry_kind != MS_CACHE_ENTRY_KIND_SOURCE &&
      entry_kind != MS_CACHE_ENTRY_KIND_MODULE) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }
  if (display_path_length == 0) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }

  header_size = MS_CACHE_FILE_HEADER_SIZE + (size_t) display_path_length;
  if (header_size < MS_CACHE_FILE_HEADER_SIZE || header_size > buffer_length) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }
  if (payload_offset != header_size) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }
  if (payload_length > buffer_length - (size_t) payload_offset) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }
  if ((size_t) payload_offset + (size_t) payload_length != buffer_length) {
    return MS_CACHE_FILE_STATUS_FORMAT_ERROR;
  }

  display_path = (char *) malloc((size_t) display_path_length + 1u);
  if (display_path == NULL) {
    return MS_CACHE_FILE_STATUS_IO_ERROR;
  }
  memcpy(display_path,
         buffer + MS_CACHE_FILE_HEADER_SIZE,
         (size_t) display_path_length);
  display_path[display_path_length] = '\0';

  out_info->display_path = display_path;
  out_info->entry_kind = (MsCacheEntryKind) entry_kind;
  out_info->source.display_path = out_info->display_path;
  out_info->source.byte_size = source_size;
  out_info->source.modification_time = source_mtime;
  out_info->payload_offset = payload_offset;
  out_info->payload_length = payload_length;
  *out_payload_offset = (size_t) payload_offset;
  return MS_CACHE_FILE_STATUS_OK;
}

MsCacheFileStatus ms_cache_file_read(const char *cache_path,
                                     const MsCacheSourceMetadata *expected_source,
                                     MsCacheFileReadInfo *out_info,
                                     MsChunk *out_chunk) {
  uint8_t *buffer = NULL;
  size_t buffer_length = 0;
  size_t payload_offset = 0;
  MsCacheFileReadInfo local_info;
  MsCacheFileReadInfo *info = out_info == NULL ? &local_info : out_info;
  int owns_info = out_info == NULL;
  MsCacheFileStatus status = MS_CACHE_FILE_STATUS_IO_ERROR;
  int payload_ok;

  if (out_chunk == NULL || cache_path == NULL) {
    return MS_CACHE_FILE_STATUS_INVALID_ARGUMENT;
  }

  ms_cache_file_read_info_init(info);
  ms_chunk_destroy(out_chunk);
  ms_chunk_init(out_chunk);

  if (!ms_cache_file_read_all(cache_path, &buffer, &buffer_length)) {
    if (buffer == NULL) {
      status = errno == ENOENT ? MS_CACHE_FILE_STATUS_NOT_FOUND
                               : MS_CACHE_FILE_STATUS_IO_ERROR;
    } else {
      status = MS_CACHE_FILE_STATUS_IO_ERROR;
    }
    goto done;
  }

  status = ms_cache_file_validate_header(buffer, buffer_length, info, &payload_offset);
  if (status != MS_CACHE_FILE_STATUS_OK) {
    goto done;
  }

  if (expected_source != NULL) {
    if (info->source.byte_size != expected_source->byte_size ||
        info->source.modification_time != expected_source->modification_time) {
      status = MS_CACHE_FILE_STATUS_STALE_SOURCE;
      goto done;
    }
  }

  payload_ok = ms_chunk_codec_read(buffer + payload_offset,
                                   (size_t) info->payload_length,
                                   out_chunk);
  if (!payload_ok) {
    status = MS_CACHE_FILE_STATUS_FORMAT_ERROR;
    goto done;
  }

  status = MS_CACHE_FILE_STATUS_OK;

done:
  free(buffer);
  if (status != MS_CACHE_FILE_STATUS_OK) {
    ms_cache_file_read_info_destroy(info);
    ms_chunk_destroy(out_chunk);
    ms_chunk_init(out_chunk);
  } else if (owns_info) {
    ms_cache_file_read_info_destroy(info);
  }
  return status;
}

static int ms_cache_file_write_header(uint8_t *buffer,
                                      size_t buffer_length,
                                      const MsCacheFileWriteInfo *info,
                                      uint32_t display_path_length,
                                      uint64_t payload_length) {
  uint64_t payload_offset;

  if (buffer == NULL || info == NULL || info->source.display_path == NULL) {
    return 0;
  }

  payload_offset = MS_CACHE_FILE_HEADER_SIZE + (uint64_t) display_path_length;

  memcpy(buffer + MS_CACHE_FILE_MAGIC_OFFSET, ms_cache_magic, MS_CACHE_MAGIC_SIZE);
  if (!ms_cache_write_u16_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_FORMAT_VERSION_OFFSET,
                             MS_CACHE_FORMAT_VERSION) ||
      !ms_cache_write_u16_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_ABI_VERSION_OFFSET,
                             MS_CACHE_ABI_VERSION) ||
      !ms_cache_write_u16_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_ENTRY_KIND_OFFSET,
                             (uint16_t) info->entry_kind) ||
      !ms_cache_write_u16_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_RESERVED_OFFSET,
                             0u) ||
      !ms_cache_write_u32_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_DISPLAY_PATH_LENGTH_OFFSET,
                             display_path_length) ||
      !ms_cache_write_u64_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_SOURCE_SIZE_OFFSET,
                             info->source.byte_size) ||
      !ms_cache_write_i64_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_SOURCE_MTIME_OFFSET,
                             info->source.modification_time) ||
      !ms_cache_write_u64_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_PAYLOAD_OFFSET_OFFSET,
                             payload_offset) ||
      !ms_cache_write_u64_le(buffer,
                             buffer_length,
                             MS_CACHE_FILE_PAYLOAD_LENGTH_OFFSET,
                             payload_length)) {
    return 0;
  }

  return 1;
}

static int ms_cache_file_make_temporary_path(const char *cache_path,
                                             char **out_temporary_path) {
  size_t cache_path_length;
  char *temporary_path;

  if (cache_path == NULL || out_temporary_path == NULL) {
    return 0;
  }

  cache_path_length = strlen(cache_path);
  temporary_path = (char *) malloc(cache_path_length + 5u);
  if (temporary_path == NULL) {
    return 0;
  }

  memcpy(temporary_path, cache_path, cache_path_length);
  memcpy(temporary_path + cache_path_length, ".tmp", 5u);
  *out_temporary_path = temporary_path;
  return 1;
}

static int ms_cache_file_replace(const char *temporary_path,
                                 const char *final_path) {
  if (temporary_path == NULL || final_path == NULL) {
    return 0;
  }

#if defined(_WIN32)
  return MoveFileExA(temporary_path,
                     final_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(temporary_path, final_path) == 0;
#endif
}

static int ms_cache_file_write_buffer(FILE *file, const MsBuffer *buffer) {
  if (buffer == NULL) {
    return 0;
  }
  if (buffer->length == 0) {
    return 1;
  }
  return fwrite(buffer->data, 1, buffer->length, file) == buffer->length;
}

MsCacheFileStatus ms_cache_file_write(const char *cache_path,
                                      const MsCacheFileWriteInfo *info,
                                      const MsChunk *chunk) {
  MsBuffer payload;
  uint8_t *header = NULL;
  size_t header_length = 0;
  size_t display_path_length;
  char *temporary_path = NULL;
  FILE *file = NULL;
  MsCacheFileStatus status = MS_CACHE_FILE_STATUS_IO_ERROR;

  if (cache_path == NULL || info == NULL || chunk == NULL ||
      info->source.display_path == NULL || info->source.display_path[0] == '\0' ||
      (info->entry_kind != MS_CACHE_ENTRY_KIND_SOURCE &&
       info->entry_kind != MS_CACHE_ENTRY_KIND_MODULE)) {
    return MS_CACHE_FILE_STATUS_INVALID_ARGUMENT;
  }

  ms_buffer_init(&payload);
  if (!ms_chunk_codec_write(chunk, &payload)) {
    status = MS_CACHE_FILE_STATUS_WRITE_ERROR;
    goto done;
  }

  display_path_length = strlen(info->source.display_path);
  if (display_path_length > UINT32_MAX) {
    status = MS_CACHE_FILE_STATUS_INVALID_ARGUMENT;
    goto done;
  }

  header_length = MS_CACHE_FILE_HEADER_SIZE + display_path_length;
  header = (uint8_t *) calloc(1u, header_length);
  if (header == NULL) {
    status = MS_CACHE_FILE_STATUS_IO_ERROR;
    goto done;
  }

  if (!ms_cache_file_write_header(header,
                                  header_length,
                                  info,
                                  (uint32_t) display_path_length,
                                  (uint64_t) payload.length)) {
    status = MS_CACHE_FILE_STATUS_WRITE_ERROR;
    goto done;
  }
  memcpy(header + MS_CACHE_FILE_HEADER_SIZE,
         info->source.display_path,
         display_path_length);

  if (!ms_cache_file_create_directories(cache_path)) {
    status = MS_CACHE_FILE_STATUS_DIRECTORY_ERROR;
    goto done;
  }

  if (!ms_cache_file_make_temporary_path(cache_path, &temporary_path)) {
    status = MS_CACHE_FILE_STATUS_IO_ERROR;
    goto done;
  }

#if defined(_WIN32)
  if (fopen_s(&file, temporary_path, "wb") != 0) {
    file = NULL;
  }
#else
  file = fopen(temporary_path, "wb");
#endif
  if (file == NULL) {
    status = MS_CACHE_FILE_STATUS_IO_ERROR;
    goto done;
  }

  if (fwrite(header, 1, header_length, file) != header_length ||
      !ms_cache_file_write_buffer(file, &payload) ||
      fclose(file) != 0) {
    status = MS_CACHE_FILE_STATUS_WRITE_ERROR;
    file = NULL;
    goto done;
  }
  file = NULL;

  if (!ms_cache_file_replace(temporary_path, cache_path)) {
    status = MS_CACHE_FILE_STATUS_WRITE_ERROR;
    goto done;
  }

  status = MS_CACHE_FILE_STATUS_OK;

done:
  if (file != NULL) {
    fclose(file);
  }
  if (status != MS_CACHE_FILE_STATUS_OK && temporary_path != NULL) {
    remove(temporary_path);
  }
  free(temporary_path);
  free(header);
  ms_buffer_destroy(&payload);
  return status;
}
