#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "common/common.h"

_Static_assert(sizeof(ms_u8) == 1, "ms_u8 must be 8 bits");
_Static_assert(sizeof(ms_i32) == 4, "ms_i32 must be 32 bits");
_Static_assert(sizeof(ms_u32) == 4, "ms_u32 must be 32 bits");
_Static_assert(sizeof(ms_i64) == 8, "ms_i64 must be 64 bits");
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
}

static void testFnv1a64(void) {
  CHECK(msFnv1a64("", 0) == 0xcbf29ce484222325ULL);
  CHECK(msFnv1a64("a", 1) == 0xaf63dc4c8601ec8cULL);
  CHECK(msFnv1a64("foobar", 6) == 0x85944171f73967e8ULL);
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
  free(arr);
}

int main(void) {
  testFnv1a32();
  testFnv1a64();
  testArrayPush();
  if (gFailures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", gFailures);
  return 1;
}
