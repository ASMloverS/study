#ifndef MSLANGC_CACHE_SOURCE_LOADER_H_
#define MSLANGC_CACHE_SOURCE_LOADER_H_

#include <stddef.h>

#include "ms/cache/cache_format.h"
#include "ms/diag.h"
#include "ms/runtime/chunk.h"

typedef enum MsSourceLoadStatus {
  MS_SOURCE_LOAD_STATUS_OK = 0,
  MS_SOURCE_LOAD_STATUS_INVALID_ARGUMENT,
  MS_SOURCE_LOAD_STATUS_IO_ERROR,
  MS_SOURCE_LOAD_STATUS_COMPILE_ERROR
} MsSourceLoadStatus;

typedef struct MsSourceLoadOptions {
  int cache_enabled;
  MsCacheEntryKind entry_kind;
} MsSourceLoadOptions;

typedef struct MsSourceLoadResult {
  MsChunk chunk;
  MsCacheSourceMetadata source;
  char *cache_path;
  int loaded_from_cache;
} MsSourceLoadResult;

void ms_source_load_options_init(MsSourceLoadOptions *options);
void ms_source_load_result_init(MsSourceLoadResult *result);
void ms_source_load_result_destroy(MsSourceLoadResult *result);

MsSourceLoadStatus ms_source_load_source(const char *source_path,
                                         const MsSourceLoadOptions *options,
                                         MsDiagnosticList *diagnostics,
                                         MsSourceLoadResult *out_result);

#endif
