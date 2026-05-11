#pragma once
#include "ms/object.h"
#include "ms/fs_util.h"
#include <stdbool.h>
#include <stdint.h>

#define MS_MSC_MAGIC    "MSC\0"
#define MS_MSC_VERSION  3

#define MS_CACHE_MTIME  0u   /* validate by (src_size, src_mtime_ns) -- default */
#define MS_CACHE_HASH   1u   /* validate by FNV-1a hash of source               */

/*
 * 32-byte header, no padding (field order chosen to avoid alignment gaps):
 *   char[4]   offset 0
 *   uint32_t  offset 4
 *   uint64_t  offset 8   (8-byte aligned)
 *   int64_t   offset 16
 *   uint32_t  offset 24
 *   uint32_t  offset 28
 */
typedef struct {
    char     magic[4];       /* "MSC\0"                                         */
    uint32_t version;        /* bytecode format version; mismatch => cache miss  */
    uint64_t src_size;       /* source file size in bytes   (mtime mode)         */
    int64_t  src_mtime_ns;   /* source mtime, nanoseconds   (mtime mode)         */
    uint32_t src_hash;       /* FNV-1a of source            (hash  mode)         */
    uint32_t flags;          /* bit 0: MS_CACHE_MTIME(0) or MS_CACHE_HASH(1)    */
} MsMscHeader;               /* 32 bytes                                         */

struct MsVM;

/*
 * ms_serialize      - write fn + hdr to path.
 * ms_deserialize    - load fn from path; validates header against the three
 *                     expected values. Which values are checked depends on
 *                     hdr.flags stored in the file:
 *                       MTIME: checks src_size + src_mtime_ns
 *                       HASH:  checks src_hash
 *                     Pass 0 for unused parameters.
 * ms_cache_path_for - map "dir/script.ms" => "dir/__mscache__/script.msc".
 *                     Returns false if out buffer is too small.
 * ms_compile_cached - load from __mscache__/<basename>.msc or compile + cache.
 *                     flags: MS_CACHE_MTIME (default) or MS_CACHE_HASH.
 *                     mtime mode: source file is never opened on cache hit.
 *                     Write failures are silently ignored.
 */
bool           ms_serialize(MsObjFunction* fn, const char* path,
                             const MsMscHeader* hdr);
MsObjFunction* ms_deserialize(struct MsVM* vm, const char* path,
                               uint64_t src_size, int64_t src_mtime_ns,
                               uint32_t src_hash);
bool           ms_cache_path_for(const char* src_path, char* out, size_t cap);
MsObjFunction* ms_compile_cached(struct MsVM* vm, const char* src_path,
                                   uint32_t flags);
