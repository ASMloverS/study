#pragma once
#include "ms/object.h"
#include <stdbool.h>
#include <stdint.h>

#define MS_MSC_MAGIC   "MSC\0"
#define MS_MSC_VERSION 1

typedef struct {
    char     magic[4];
    uint32_t version;
    uint32_t flags;
    uint32_t src_hash;
} MsMscHeader;

struct MsVM;

bool           ms_serialize(MsObjFunction* fn, const char* path, uint32_t src_hash);
MsObjFunction* ms_deserialize(struct MsVM* vm, const char* path, uint32_t src_hash);
MsObjFunction* ms_compile_cached(struct MsVM* vm, const char* source,
                                  const char* src_path);
