#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "common/common.h"
#include "plat/plat_atomic.h"
#include "plat/plat_thread.h"
#include "plat/plat_time.h"

_Static_assert(sizeof(ms_u8) == 1, "ms_u8 must be 8 bits");
_Static_assert(sizeof(ms_i32) == 4, "ms_i32 must be 32 bits");
_Static_assert(sizeof(ms_u32) == 4, "ms_u32 must be 32 bits");
_Static_assert(sizeof(ms_i64) == 8, "ms_i64 must be 64 bits");
_Static_assert(sizeof(ms_u64) == 8, "ms_u64 must be 64 bits");
_Static_assert(sizeof(ms_f64) == 8, "ms_f64 must be 64 bits");

static int gFailures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      gFailures++;                                                    \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static void testFnv1a32(void) {
  CHECK(msFnv1a32("", 0) == 0x811c9dc5u);
  CHECK(msFnv1a32("a", 1) == 0xe40c292cu);
  CHECK(msFnv1a32("foobar", 6) == 0xbf9cf968u);
  CHECK(msFnv1a32("a\0b", 3) == 0x10f3abd2u);
}

static void testFnv1a64(void) {
  CHECK(msFnv1a64("", 0) == 0xcbf29ce484222325ULL);
  CHECK(msFnv1a64("a", 1) == 0xaf63dc4c8601ec8cULL);
  CHECK(msFnv1a64("foobar", 6) == 0x85944171f73967e8ULL);
  CHECK(msFnv1a64("a\0b", 3) == 0xe5d29919042666b2ULL);
}

static void testArrayPush(void) {
  int* arr = NULL;
  int count = 0;
  int cap = 0;
  for (int i = 0; i < 100; i++) {
    MS_ARRAY_PUSH(arr, count, cap, i * 3);
  }
  CHECK(count == 100);
  CHECK(cap >= 100);
  bool values = true;
  for (int i = 0; i < 100; i++) {
    if (arr[i] != i * 3) {
      values = false;
    }
  }
  CHECK(values);
  int oldCap = cap;
  MS_ARRAY_PUSH(arr, count, cap, 42);
  CHECK(cap >= oldCap);
  while (count < cap) {
    MS_ARRAY_PUSH(arr, count, cap, 0);
  }
  oldCap = cap;
  MS_ARRAY_PUSH(arr, count, cap, 0);
  CHECK(cap > oldCap);
  free(arr);
}

static void testAtomicSmoke(void) {
  PlatAtomic a;
  platAtomicInit(&a, 0);
  platAtomicStore(&a, 42, memory_order_relaxed);
  CHECK(platAtomicLoad(&a, memory_order_acquire) == 42);
  CHECK(platAtomicAdd(&a, 1, memory_order_relaxed) == 42);
  CHECK(platAtomicLoad(&a, memory_order_relaxed) == 43);
  ms_i64 expected = 43;
  CHECK(platAtomicCas(&a, &expected, 100, memory_order_acq_rel));
  CHECK(platAtomicLoad(&a, memory_order_relaxed) == 100);
  ms_i64 wrong = 999;
  CHECK(!platAtomicCas(&a, &wrong, 0, memory_order_acq_rel));
  CHECK(platAtomicLoad(&a, memory_order_relaxed) == 100);
}

typedef struct CounterArgs {
  PlatMutex mutex;
  int value;
} CounterArgs;

static void incrementWorker(void* arg) {
  CounterArgs* counter = arg;
  for (int i = 0; i < 10000; i++) {
    platMutexLock(&counter->mutex);
    counter->value++;
    platMutexUnlock(&counter->mutex);
  }
}

static void testThreadSmoke(void) {
  CounterArgs counter;
  CHECK(platMutexInit(&counter.mutex));
  counter.value = 0;
  PlatThread* threads[4];
  for (int i = 0; i < 4; i++) {
    if (!platThreadCreate(&threads[i], incrementWorker, &counter)) {
      CHECK(false);
      platMutexDestroy(&counter.mutex);
      return;
    }
  }
  for (int i = 0; i < 4; i++) {
    platThreadJoin(threads[i]);
    platThreadDestroy(threads[i]);
  }
  CHECK(counter.value == 40000);
  platMutexDestroy(&counter.mutex);
}

typedef struct AtomicArgs {
  PlatAtomic* counter;
} AtomicArgs;

static void atomicAddWorker(void* arg) {
  AtomicArgs* args = arg;
  for (int i = 0; i < 10000; i++) {
    platAtomicAdd(args->counter, 1, memory_order_relaxed);
  }
}

static void testAtomicConcurrent(void) {
  PlatAtomic counter;
  platAtomicInit(&counter, 0);
  AtomicArgs args;
  args.counter = &counter;
  PlatThread* threads[4];
  for (int i = 0; i < 4; i++) {
    if (!platThreadCreate(&threads[i], atomicAddWorker, &args)) {
      CHECK(false);
      return;
    }
  }
  for (int i = 0; i < 4; i++) {
    platThreadJoin(threads[i]);
    platThreadDestroy(threads[i]);
  }
  CHECK(platAtomicLoad(&counter, memory_order_acquire) == 40000);
}

static void testTimeMonotonic(void) {
  ms_i64 previous = platTimeMonotonicNs();
  for (int i = 0; i < 1000; i++) {
    ms_i64 now = platTimeMonotonicNs();
    CHECK(now >= previous);
    previous = now;
  }
}

int main(void) {
  testFnv1a32();
  testFnv1a64();
  testArrayPush();
  testAtomicSmoke();
  testThreadSmoke();
  testAtomicConcurrent();
  testTimeMonotonic();
  if (gFailures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", gFailures);
  return 1;
}
