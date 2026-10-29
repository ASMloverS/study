// i64 溢出检测三件套（T016，22 §5）：GCC/Clang 走内建；MSVC 无等价内建，手写 UB 安全检查
#ifndef INCLUDE_MS_PLAT_PLAT_MATH_H_
#define INCLUDE_MS_PLAT_PLAT_MATH_H_

#include <stdbool.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)

static inline bool msAddOverflowI64(int64_t a, int64_t b, int64_t* r) {
  return __builtin_add_overflow(a, b, r);
}

static inline bool msSubOverflowI64(int64_t a, int64_t b, int64_t* r) {
  return __builtin_sub_overflow(a, b, r);
}

static inline bool msMulOverflowI64(int64_t a, int64_t b, int64_t* r) {
  return __builtin_mul_overflow(a, b, r);
}

#else  // MSVC 等：分支检查（与原 vm.c 手写同型）

static inline bool msAddOverflowI64(int64_t a, int64_t b, int64_t* r) {
  *r = (int64_t)((uint64_t)a + (uint64_t)b);
  return (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b);
}

static inline bool msSubOverflowI64(int64_t a, int64_t b, int64_t* r) {
  *r = (int64_t)((uint64_t)a - (uint64_t)b);
  return (b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b);
}

static inline uint64_t msAbsU64(int64_t v) {
  return v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
}

static inline bool msMulOverflowI64(int64_t a, int64_t b, int64_t* r) {
  *r = 0;
  if (a == 0 || b == 0) {
    return false;
  }
  uint64_t ua = msAbsU64(a);
  uint64_t ub = msAbsU64(b);
  if (ua > (uint64_t)INT64_MAX / ub) {
    return true;
  }
  uint64_t um = ua * ub;  // |积| ≤ INT64_MAX，正负号均可表示
  *r = (a < 0) != (b < 0) ? -(int64_t)um : (int64_t)um;
  return false;
}

#endif

#endif  // INCLUDE_MS_PLAT_PLAT_MATH_H_
