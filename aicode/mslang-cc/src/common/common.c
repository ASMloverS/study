#include "common.h"

ms_u32 msFnv1a32(const void* data, ms_u64 len) {
  const ms_u8* bytes = data;
  ms_u32 hash = 2166136261u;
  for (ms_u64 i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

ms_u64 msFnv1a64(const void* data, ms_u64 len) {
  const ms_u8* bytes = data;
  ms_u64 hash = 14695981039346656037ULL;
  for (ms_u64 i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}
