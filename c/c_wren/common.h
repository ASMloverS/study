/*
 * Copyright (c) 2024 ASMlover. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list ofconditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materialsprovided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef CWREN_COMMON_H
#define CWREN_COMMON_H

#if !defined(CWREN_UNUSED)
# define CWREN_UNUSED(x) ((void)x)
#endif

#if defined(__GUNC__) || defined(__clang__)
# define CWREN_GUNC
#else
# define CWREN_MSVC
#endif

#if !defined(CWREN_NAN_TAGGING)
# define CWREN_NAN_TAGGING 1
#endif

#if !defined(CWREN_COMPUTED_GOTO)
# if defined(_MSC_VER)
#   define CWREN_COMPUTED_GOTO 0
# else
#   define CWREN_COMPUTED_GOTO 1
# endif
#endif

#if !defined(CWREN_OPT_META)
# define CWREN_OPT_META 1
#endif

#if !defined(CWREN_OPT_RANDOM)
# define CWREN_OPT_RANDOM 1
#endif

#define CWREN_DEBUG_GC_STRESS           (0)
#define CWREN_DEBUG_TRACE_MEMORY        (0)
#define CWREN_DEBUG_TRACE_GC            (0)
#define CWREN_DEBUG_DUMP_COMPILED_CODE  (0)
#define CWREN_DEBUG_TRACE_INSTRUCTIONS  (0)
#define CWREN_MAX_MODULE_VARS           (65536)
#define CWREN_MAX_PARAMETERS            (16)
#define CWREN_MAX_METHOD_NAME           (64)
#define CWREN_MAX_METHOD_SIGNATURE      (CWREN_MAX_METHOD_NAME + (CWREN_MAX_PARAMETERS * 2) + 6)
#define CWREN_MAX_VARIABLE_NAME         (64)
#define CWREN_MAX_FIELDS                (255)

#define ALLOCATE(vm, type)\
  ((type*)cwrenReallocate(vm, NULL, 0, sizeof(type)))

#define ALLOCATE_FLEX(vm, mainType, arrayType, count)\
  ((mainType*)cwrenReallocate(vm, NULL, 0, sizeof(mainType) + sizeof(arrayType) * (count)))

#define ALLOCATE_ARRAY(vm, type, count)\
  ((type*)cwrenReallocate(vm, NULL, 0, sizeof(type) * (count)))

#define DEALLOCATE(vm, pointer)\
  cwrenReallocate(vm, pointer, 0, 0)

#if defined(_MSC_VER) && !defined(__cplusplus)
# define inline __inline
#endif

#if __STDC_VERSION__ >= 199901L
# define CWREN_FLEXIBLE_ARRAY
#else
# define CWREN_FLEXIBLE_ARRAY 0
#endif

#if defined(DEBUG)
# include <stdio.h>
# define ASSERT(condition, message)\
  do {\
    if (!(condition)) {\
      fprintf(stderr, "[%s:%s] Assert failed in %s(): %s\n", __FILE__, __LINE__, __func__, (message));\
      abort();\
    }\
  } while (false)

# define UNREACHABLE()\
  do {\
    fprintf(stderr, "[%s:%s] This code should not be reached in %s()\n", __FILE__, __LINE__, __func__);\
    abort();\
  } while (false)
#else
# define ASSERT(condition, message)

# if defined(_MSC_VER)
#   define UNREACHABLE() __assume(0)
# elif (__GUNC__ > 4 || (__GUNC__ == 4 && __GUNC_MINOR__ >= 5))
#   define UNREACHABLE() __builtin_unreachable()
# else
#   define UNREACHABLE()
# endif
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t   byte_t;
typedef int8_t    i8_t;
typedef uint8_t   u8_t;
typedef int16_t   i16_t;
typedef uint16_t  u16_t;
typedef int32_t   i32_t;
typedef uint32_t  u32_t;
typedef int64_t   i64_t;
typedef uint64_t  u64_t;
typedef size_t    sz_t;
#if defined(CWREN_GUNC)
typedef ssizt_t   ssz_t;
#else
typedef int64_t   ssz_t;
#endif

#endif
