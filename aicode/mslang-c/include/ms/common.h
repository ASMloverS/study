#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef uint8_t  ms_u8;
typedef uint16_t ms_u16;
typedef uint32_t ms_u32;
typedef uint64_t ms_u64;
typedef int8_t   ms_i8;
typedef int32_t  ms_i32;
typedef int64_t  ms_i64;
typedef size_t   ms_sz;

#define MS_UNUSED(x) ((void)(x))

#if defined(_MSC_VER)
#  define ms_strdup _strdup
#else
#  define ms_strdup strdup
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define MS_LIKELY(x)   __builtin_expect(!!(x), 1)
  #define MS_UNLIKELY(x) __builtin_expect(!!(x), 0)
  #define MS_INLINE      static inline __attribute__((unused))
#else
  #define MS_LIKELY(x)   (x)
  #define MS_UNLIKELY(x) (x)
  #define MS_INLINE      static inline
#endif
