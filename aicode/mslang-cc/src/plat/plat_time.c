#include "plat_time.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// QPC 频率恒定；先除后乘避免 counter * 1e9 溢出 int64
static ms_i64 qpcToNs(LONGLONG counter, LONGLONG freq) {
  return (counter / freq) * 1000000000LL + (counter % freq) * 1000000000LL / freq;
}

ms_i64 platTimeMonotonicNs(void) {
  LARGE_INTEGER freq;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  return qpcToNs(counter.QuadPart, freq.QuadPart);
}

ms_i64 platTimeWallNs(void) {
  FILETIME ft;
  ULARGE_INTEGER unixEpoch;  // 1601-01-01 至 1970-01-01 的 100ns 刻度数
  GetSystemTimeAsFileTime(&ft);
  unixEpoch.LowPart = ft.dwLowDateTime;
  unixEpoch.HighPart = ft.dwHighDateTime;
  return (ms_i64)(unixEpoch.QuadPart - 116444736000000000ULL) * 100LL;
}

#else
#include <time.h>

static ms_i64 timespecToNs(const struct timespec* ts) { return (ms_i64)ts->tv_sec * 1000000000LL + ts->tv_nsec; }

ms_i64 platTimeMonotonicNs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return timespecToNs(&ts);
}

ms_i64 platTimeWallNs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return timespecToNs(&ts);
}

#endif

ms_i64 platTimeMonotonicMs(void) { return platTimeMonotonicNs() / 1000000LL; }

ms_i64 platTimeWallMs(void) { return platTimeWallNs() / 1000000LL; }
