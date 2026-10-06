#ifndef INCLUDE_MS_PLAT_PLAT_ATOMIC_H_
#define INCLUDE_MS_PLAT_PLAT_ATOMIC_H_

#include <stdbool.h>
#include <stdatomic.h>

#include "common/common.h"

typedef struct PlatAtomic {
  _Atomic ms_i64 value;
} PlatAtomic;

static inline void platAtomicInit(PlatAtomic* a, ms_i64 value) { atomic_init(&a->value, value); }

static inline ms_i64 platAtomicLoad(const PlatAtomic* a, memory_order order) {
  return atomic_load_explicit(&a->value, order);
}

static inline void platAtomicStore(PlatAtomic* a, ms_i64 value, memory_order order) {
  atomic_store_explicit(&a->value, value, order);
}

static inline ms_i64 platAtomicAdd(PlatAtomic* a, ms_i64 delta, memory_order order) {
  return atomic_fetch_add_explicit(&a->value, delta, order);
}

// CAS 失败序禁止 release/acq_rel（C11 §7.17.7.4 约束，降为 acquire）；expected 为指针以便读回失败观测值
static inline bool platAtomicCas(PlatAtomic* a, ms_i64* expected, ms_i64 desired, memory_order order) {
  memory_order fail = (order == memory_order_acq_rel || order == memory_order_release) ? memory_order_acquire : order;
  (void)fail;  // MSVC 单序 CAS 内建丢弃失败序实参（否则 C4189）；GCC/Clang 正常消费
  return atomic_compare_exchange_strong_explicit(&a->value, expected, desired, order, fail);
}

#endif
