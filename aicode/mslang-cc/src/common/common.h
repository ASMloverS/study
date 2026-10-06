#ifndef INCLUDE_MS_COMMON_COMMON_H_
#define INCLUDE_MS_COMMON_COMMON_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t ms_u8;
typedef int32_t ms_i32;
typedef uint32_t ms_u32;
typedef int64_t ms_i64;
typedef uint64_t ms_u64;
typedef double ms_f64;

#define MS_VERSION "0.0.1 (dev)"

// 仅内部不变量断言：Release（NDEBUG）编译剔除
#if defined(NDEBUG)
#define MS_ASSERT(cond) ((void)0)
#else
#define MS_ASSERT(cond) assert(cond)
#endif

// 统一动态数组增长：容量从 0 起按 2 倍扩张，arr/count/cap 均为左值
#define MS_ARRAY_PUSH(arr, count, cap, elem)                  \
  do {                                                        \
    if ((count) == (cap)) {                                   \
      (cap) = (cap) == 0 ? 8 : (cap) * 2;                     \
      (arr) = realloc((arr), sizeof(*(arr)) * (size_t)(cap)); \
      MS_ASSERT((arr) != NULL);                               \
    }                                                         \
    (arr)[(count)] = (elem);                                  \
    (count)++;                                                \
  } while (0)

ms_u32 msFnv1a32(const void* data, ms_u64 len);
ms_u64 msFnv1a64(const void* data, ms_u64 len);

#endif
