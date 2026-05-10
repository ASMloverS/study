#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t size;
    int64_t  mtime_ns;  /* nanoseconds since Unix epoch, Y2038-safe */
} MsFileMeta;

bool ms_fs_stat(const char* path, MsFileMeta* out);
bool ms_fs_mkdir(const char* path);                          /* EEXIST => true */
bool ms_fs_atomic_rename(const char* tmp, const char* dst); /* replaces dst */
bool ms_fs_unlink(const char* path);
