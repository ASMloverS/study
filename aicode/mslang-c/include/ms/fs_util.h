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

/* Split s by delim into heap-allocated strdup'd tokens.
   Writes malloc'd array of char* into *out_parts. Returns token count.
   Caller must free each element and then free(*out_parts). */
int ms_string_split(const char* s, char delim, char*** out_parts);
