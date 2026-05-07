#include "ms/cache/source_loader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "ms/cache/cache_file.h"
#include "ms/frontend/lowering.h"

static char *ms_source_loader_strdup(const char *text) {
  char *copy;
  size_t length;

  if (text == NULL) {
    return NULL;
  }

  length = strlen(text);
  copy = (char *) malloc(length + 1u);
  if (copy == NULL) {
    return NULL;
  }

  memcpy(copy, text, length + 1u);
  return copy;
}

static const char *ms_source_loader_find_extension(const char *path) {
  const char *cursor;
  const char *last_dot = NULL;

  if (path == NULL) {
    return NULL;
  }

  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') {
      last_dot = cursor;
    }
    if (*cursor == '/' || *cursor == '\\') {
      last_dot = NULL;
    }
  }

  return last_dot;
}

static int ms_source_loader_is_supported_source_path(const char *path) {
  const char *extension = ms_source_loader_find_extension(path);

  return extension != NULL && strcmp(extension, ".ms") == 0;
}

static char *ms_source_loader_read_file(const char *path) {
  FILE *file;
  long size;
  size_t read_size;
  char *buffer;

  if (path == NULL) {
    return NULL;
  }

#if defined(_MSC_VER)
  if (fopen_s(&file, path, "rb") != 0) {
    file = NULL;
  }
#else
  file = fopen(path, "rb");
#endif
  if (file == NULL) {
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }

  size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  buffer = (char *) malloc((size_t) size + 1u);
  if (buffer == NULL) {
    fclose(file);
    return NULL;
  }

  read_size = fread(buffer, 1, (size_t) size, file);
  fclose(file);
  if (read_size != (size_t) size) {
    free(buffer);
    return NULL;
  }

  buffer[size] = '\0';
  return buffer;
}

static int ms_source_loader_stat_source(const char *path,
                                        MsCacheSourceMetadata *out_source) {
#if defined(_WIN32)
  struct _stat info;
#else
  struct stat info;
#endif

  if (path == NULL || out_source == NULL) {
    return 0;
  }

#if defined(_WIN32)
  if (_stat(path, &info) != 0) {
    return 0;
  }
#else
  if (stat(path, &info) != 0) {
    return 0;
  }
#endif

  out_source->display_path = path;
  out_source->byte_size = (uint64_t) info.st_size;
  out_source->modification_time = (int64_t) info.st_mtime;
  return 1;
}

static void ms_source_loader_clear_result(MsSourceLoadResult *result) {
  if (result == NULL) {
    return;
  }

  ms_chunk_destroy(&result->chunk);
  ms_chunk_init(&result->chunk);
  free((void *) result->source.display_path);
  free(result->cache_path);
  result->source.display_path = NULL;
  result->source.byte_size = 0;
  result->source.modification_time = 0;
  result->cache_path = NULL;
  result->loaded_from_cache = 0;
}

void ms_source_load_options_init(MsSourceLoadOptions *options) {
  if (options == NULL) {
    return;
  }

  options->cache_enabled = 1;
  options->entry_kind = MS_CACHE_ENTRY_KIND_SOURCE;
}

void ms_source_load_result_init(MsSourceLoadResult *result) {
  if (result == NULL) {
    return;
  }

  ms_chunk_init(&result->chunk);
  result->source.display_path = NULL;
  result->source.byte_size = 0;
  result->source.modification_time = 0;
  result->cache_path = NULL;
  result->loaded_from_cache = 0;
}

void ms_source_load_result_destroy(MsSourceLoadResult *result) {
  if (result == NULL) {
    return;
  }

  ms_source_loader_clear_result(result);
}

static int ms_source_loader_try_cache_read(const char *cache_path,
                                           MsCacheEntryKind entry_kind,
                                           const MsCacheSourceMetadata *source,
                                           MsSourceLoadResult *out_result) {
  MsCacheFileReadInfo read_info;
  MsCacheFileStatus status;

  if (cache_path == NULL || source == NULL || out_result == NULL) {
    return 0;
  }

  ms_cache_file_read_info_init(&read_info);
  status = ms_cache_file_read(cache_path, source, &read_info, &out_result->chunk);
  if (status != MS_CACHE_FILE_STATUS_OK) {
    ms_cache_file_read_info_destroy(&read_info);
    return 0;
  }

  if (read_info.entry_kind != entry_kind) {
    ms_cache_file_read_info_destroy(&read_info);
    ms_chunk_destroy(&out_result->chunk);
    ms_chunk_init(&out_result->chunk);
    return 0;
  }

  out_result->source = read_info.source;
  out_result->source.display_path = read_info.display_path;
  read_info.display_path = NULL;
  out_result->loaded_from_cache = 1;
  ms_cache_file_read_info_destroy(&read_info);
  return 1;
}

static void ms_source_loader_attempt_cache_write(const char *cache_path,
                                                 MsCacheEntryKind entry_kind,
                                                 const MsCacheSourceMetadata *source,
                                                 const MsChunk *chunk) {
  MsCacheFileWriteInfo write_info;

  if (cache_path == NULL || source == NULL || chunk == NULL) {
    return;
  }

  write_info.entry_kind = entry_kind;
  write_info.source = *source;
  (void) ms_cache_file_write(cache_path, &write_info, chunk);
}

MsSourceLoadStatus ms_source_load_source(const char *source_path,
                                         const MsSourceLoadOptions *options,
                                         MsDiagnosticList *diagnostics,
                                         MsSourceLoadResult *out_result) {
  char *cache_path = NULL;
  char *source_text = NULL;
  MsCacheSourceMetadata source = {0};
  int have_source_metadata = 0;
  int have_cache_path = 0;

  if (source_path == NULL || options == NULL || diagnostics == NULL ||
      out_result == NULL) {
    return MS_SOURCE_LOAD_STATUS_INVALID_ARGUMENT;
  }

  ms_source_load_result_init(out_result);
  ms_diag_list_clear(diagnostics);

  if (!ms_source_loader_is_supported_source_path(source_path)) {
    return MS_SOURCE_LOAD_STATUS_INVALID_ARGUMENT;
  }

  if (options->cache_enabled) {
    if (ms_cache_derive_path(source_path, &cache_path)) {
      have_cache_path = 1;
      have_source_metadata = ms_source_loader_stat_source(source_path, &source);
      if (have_source_metadata &&
          ms_source_loader_try_cache_read(cache_path,
                                          options->entry_kind,
                                          &source,
                                          out_result)) {
        out_result->cache_path = cache_path;
        return MS_SOURCE_LOAD_STATUS_OK;
      }
    }
  }

  source_text = ms_source_loader_read_file(source_path);
  if (source_text == NULL) {
    ms_source_loader_clear_result(out_result);
    free(cache_path);
    return MS_SOURCE_LOAD_STATUS_IO_ERROR;
  }

  if (ms_compile_source(source_path, source_text, &out_result->chunk, diagnostics) !=
      MS_COMPILE_RESULT_OK) {
    free(source_text);
    ms_source_loader_clear_result(out_result);
    free(cache_path);
    return MS_SOURCE_LOAD_STATUS_COMPILE_ERROR;
  }
  free(source_text);

  out_result->source.display_path = ms_source_loader_strdup(source_path);
  if (out_result->source.display_path == NULL) {
    ms_source_loader_clear_result(out_result);
    free(cache_path);
    return MS_SOURCE_LOAD_STATUS_IO_ERROR;
  }

  if (have_source_metadata) {
    out_result->source.byte_size = source.byte_size;
    out_result->source.modification_time = source.modification_time;
  }

  if (!have_cache_path) {
    free(cache_path);
    return MS_SOURCE_LOAD_STATUS_OK;
  }

  ms_source_loader_attempt_cache_write(cache_path,
                                       options->entry_kind,
                                       &out_result->source,
                                       &out_result->chunk);
  out_result->cache_path = cache_path;
  return MS_SOURCE_LOAD_STATUS_OK;
}
