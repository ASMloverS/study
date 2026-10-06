#include "plat_thread.h"

#include <stdlib.h>

#if defined(_WIN32)
#include <process.h>
#endif

typedef struct ThreadStart {
  PlatThreadFn fn;
  void* arg;
} ThreadStart;

#if defined(_WIN32)

struct PlatThread {
  HANDLE handle;
};

static unsigned __stdcall threadTrampoline(void* param) {
  ThreadStart start = *(ThreadStart*)param;
  free(param);
  start.fn(start.arg);
  return 0;
}

bool platThreadCreate(PlatThread** t, PlatThreadFn fn, void* arg) {
  *t = malloc(sizeof(PlatThread));
  if (*t == NULL) {
    return false;
  }
  ThreadStart* start = malloc(sizeof(ThreadStart));
  if (start == NULL) {
    free(*t);
    *t = NULL;
    return false;
  }
  start->fn = fn;
  start->arg = arg;
  (*t)->handle = (HANDLE)_beginthreadex(NULL, 0, threadTrampoline, start, 0, NULL);
  if ((*t)->handle == NULL) {
    free(start);
    free(*t);
    *t = NULL;
    return false;
  }
  return true;
}

void platThreadJoin(PlatThread* t) { WaitForSingleObject(t->handle, INFINITE); }

void platThreadDestroy(PlatThread* t) {
  if (t == NULL) {
    return;
  }
  CloseHandle(t->handle);
  free(t);
}

bool platMutexInit(PlatMutex* m) {
  InitializeSRWLock(&m->lock);
  return true;
}

void platMutexDestroy(PlatMutex* m) { (void)m; }

void platMutexLock(PlatMutex* m) { AcquireSRWLockExclusive(&m->lock); }

bool platMutexTryLock(PlatMutex* m) { return TryAcquireSRWLockExclusive(&m->lock) != 0; }

void platMutexUnlock(PlatMutex* m) { ReleaseSRWLockExclusive(&m->lock); }

bool platCondInit(PlatCond* c) {
  InitializeConditionVariable(&c->cond);
  return true;
}

void platCondDestroy(PlatCond* c) { (void)c; }

void platCondWait(PlatCond* c, PlatMutex* m) { SleepConditionVariableSRW(&c->cond, &m->lock, INFINITE, 0); }

void platCondSignal(PlatCond* c) { WakeConditionVariable(&c->cond); }

void platCondBroadcast(PlatCond* c) { WakeAllConditionVariable(&c->cond); }

#else

struct PlatThread {
  pthread_t thread;
};

static void* threadTrampoline(void* param) {
  ThreadStart start = *(ThreadStart*)param;
  free(param);
  start.fn(start.arg);
  return NULL;
}

bool platThreadCreate(PlatThread** t, PlatThreadFn fn, void* arg) {
  *t = malloc(sizeof(PlatThread));
  if (*t == NULL) {
    return false;
  }
  ThreadStart* start = malloc(sizeof(ThreadStart));
  if (start == NULL) {
    free(*t);
    *t = NULL;
    return false;
  }
  start->fn = fn;
  start->arg = arg;
  if (pthread_create(&(*t)->thread, NULL, threadTrampoline, start) != 0) {
    free(start);
    free(*t);
    *t = NULL;
    return false;
  }
  return true;
}

void platThreadJoin(PlatThread* t) { pthread_join(t->thread, NULL); }

void platThreadDestroy(PlatThread* t) {
  if (t == NULL) {
    return;
  }
  free(t);
}

bool platMutexInit(PlatMutex* m) { return pthread_mutex_init(&m->mutex, NULL) == 0; }

void platMutexDestroy(PlatMutex* m) { pthread_mutex_destroy(&m->mutex); }

void platMutexLock(PlatMutex* m) { pthread_mutex_lock(&m->mutex); }

bool platMutexTryLock(PlatMutex* m) { return pthread_mutex_trylock(&m->mutex) == 0; }

void platMutexUnlock(PlatMutex* m) { pthread_mutex_unlock(&m->mutex); }

bool platCondInit(PlatCond* c) { return pthread_cond_init(&c->cond, NULL) == 0; }

void platCondDestroy(PlatCond* c) { pthread_cond_destroy(&c->cond); }

void platCondWait(PlatCond* c, PlatMutex* m) { pthread_cond_wait(&c->cond, &m->mutex); }

void platCondSignal(PlatCond* c) { pthread_cond_signal(&c->cond); }

void platCondBroadcast(PlatCond* c) { pthread_cond_broadcast(&c->cond); }

#endif
