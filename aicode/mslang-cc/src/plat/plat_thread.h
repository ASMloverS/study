#ifndef INCLUDE_MS_PLAT_PLAT_THREAD_H_
#define INCLUDE_MS_PLAT_PLAT_THREAD_H_

#include <stdbool.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct PlatThread PlatThread;
typedef void (*PlatThreadFn)(void* arg);

// 生命周期约定：create → join 恰一次 → destroy；重复 join 或未 join 即 destroy 为 UB
bool platThreadCreate(PlatThread** t, PlatThreadFn fn, void* arg);
void platThreadJoin(PlatThread* t);
void platThreadDestroy(PlatThread* t);

typedef struct PlatMutex {
#if defined(_WIN32)
  SRWLOCK lock;
#else
  pthread_mutex_t mutex;
#endif
} PlatMutex;

bool platMutexInit(PlatMutex* m);
void platMutexDestroy(PlatMutex* m);
void platMutexLock(PlatMutex* m);
bool platMutexTryLock(PlatMutex* m);  // 非阻塞尝试加锁，立即返回成功与否
void platMutexUnlock(PlatMutex* m);

typedef struct PlatCond {
#if defined(_WIN32)
  CONDITION_VARIABLE cond;
#else
  pthread_cond_t cond;
#endif
} PlatCond;

bool platCondInit(PlatCond* c);
void platCondDestroy(PlatCond* c);
void platCondWait(PlatCond* c, PlatMutex* m);  // 存在虚假唤醒，调用方必须以谓词循环包裹
void platCondSignal(PlatCond* c);
void platCondBroadcast(PlatCond* c);

#endif
