/* threadpool.c - CAPI-07: Async file IO thread pool implementation.
 *
 * Platform:
 *   Unix  - pthread_mutex_t / pthread_cond_t / pthread_t / pipe()
 *   Win32 - CRITICAL_SECTION / CONDITION_VARIABLE / CreateThread / CreateEvent
 */
#include "ms/threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ---- Platform abstractions ---- */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>

typedef CRITICAL_SECTION   ms_mutex_t;
typedef CONDITION_VARIABLE ms_cond_t;
typedef HANDLE             ms_thread_t;

static void tp_mutex_init   (ms_mutex_t* m) { InitializeCriticalSection(m); }
static void tp_mutex_destroy(ms_mutex_t* m) { DeleteCriticalSection(m); }
static void tp_mutex_lock   (ms_mutex_t* m) { EnterCriticalSection(m); }
static void tp_mutex_unlock (ms_mutex_t* m) { LeaveCriticalSection(m); }
static void tp_cond_init    (ms_cond_t*  c) { InitializeConditionVariable(c); }
static void tp_cond_destroy (ms_cond_t*  c) { (void)c; }
static void tp_cond_wait    (ms_cond_t*  c, ms_mutex_t* m) {
    SleepConditionVariableCS(c, m, INFINITE);
}
static void tp_cond_signal  (ms_cond_t*  c) { WakeConditionVariable(c); }
static void tp_cond_broadcast(ms_cond_t* c) { WakeAllConditionVariable(c); }

#else /* POSIX */
#  include <pthread.h>
#  include <unistd.h>
#  include <time.h>

typedef pthread_mutex_t ms_mutex_t;
typedef pthread_cond_t  ms_cond_t;
typedef pthread_t       ms_thread_t;

static void tp_mutex_init   (ms_mutex_t* m) { pthread_mutex_init(m, NULL); }
static void tp_mutex_destroy(ms_mutex_t* m) { pthread_mutex_destroy(m); }
static void tp_mutex_lock   (ms_mutex_t* m) { pthread_mutex_lock(m); }
static void tp_mutex_unlock (ms_mutex_t* m) { pthread_mutex_unlock(m); }
static void tp_cond_init    (ms_cond_t*  c) { pthread_cond_init(c, NULL); }
static void tp_cond_destroy (ms_cond_t*  c) { pthread_cond_destroy(c); }
static void tp_cond_wait    (ms_cond_t*  c, ms_mutex_t* m) {
    pthread_cond_wait(c, m);
}
static void tp_cond_signal  (ms_cond_t*  c) { pthread_cond_signal(c); }
static void tp_cond_broadcast(ms_cond_t* c) { pthread_cond_broadcast(c); }
#endif

/* ---- Internal helpers to access opaque pointers ---- */

static ms_mutex_t* get_mutex(MsThreadPool* tp) {
    return (ms_mutex_t*)tp->mutex_opaque;
}
static ms_cond_t* get_cond(MsThreadPool* tp) {
    return (ms_cond_t*)tp->cond_opaque;
}

static void tp_lock  (MsThreadPool* tp) { tp_mutex_lock(get_mutex(tp)); }
static void tp_unlock(MsThreadPool* tp) { tp_mutex_unlock(get_mutex(tp)); }
static void tp_wait  (MsThreadPool* tp) { tp_cond_wait(get_cond(tp), get_mutex(tp)); }
static void tp_signal(MsThreadPool* tp) { tp_cond_signal(get_cond(tp)); }
static void tp_bcast (MsThreadPool* tp) { tp_cond_broadcast(get_cond(tp)); }

/* ---- Wakeup: notify main thread ---- */

static void wakeup_main(MsThreadPool* tp) {
#ifdef _WIN32
    SetEvent((HANDLE)tp->wakeup_event);
#else
    char byte = 1;
    (void)write(tp->wakeup_w, &byte, 1);
#endif
}

/* ---- Push completed job onto done_queue (called from worker, under lock) ---- */

static void push_done_locked(MsThreadPool* tp, MsJob* job) {
    job->next = NULL;
    if (!tp->done_tail) {
        tp->done_head = tp->done_tail = job;
    } else {
        tp->done_tail->next = job;
        tp->done_tail = job;
    }
}

/* ---- Execute one job (pure blocking IO, no GC state touched) ---- */

static void execute_job(MsJob* job) {
    if (job->kind == MS_JOB_READ_FILE) {
        FILE* f = fopen(job->path, "rb");
        if (!f) {
            job->error = errno;
            return;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            job->error = errno;
            fclose(f);
            return;
        }
        long sz = ftell(f);
        if (sz < 0) { job->error = errno; fclose(f); return; }
        rewind(f);
        job->result_buf = (char*)malloc((size_t)sz + 1);
        if (!job->result_buf) { job->error = ENOMEM; fclose(f); return; }
        job->result_len = fread(job->result_buf, 1, (size_t)sz, f);
        job->result_buf[job->result_len] = '\0';
        fclose(f);
    } else { /* MS_JOB_WRITE_FILE */
        FILE* f = fopen(job->path, "wb");
        if (!f) { job->error = errno; return; }
        if (job->write_buf && job->write_len > 0)
            fwrite(job->write_buf, 1, job->write_len, f);
        fclose(f);
    }
}

/* ---- Worker thread entry point ---- */

#ifdef _WIN32
static DWORD WINAPI worker_fn(LPVOID arg) {
#else
static void* worker_fn(void* arg) {
#endif
    MsThreadPool* tp = (MsThreadPool*)arg;
    for (;;) {
        tp_lock(tp);
        while (!tp->head && !tp->shutdown)
            tp_wait(tp);
        if (tp->shutdown) { tp_unlock(tp); break; }

        MsJob* job  = tp->head;
        tp->head    = job->next;
        if (!tp->head) tp->tail = NULL;
        tp_unlock(tp);

        execute_job(job);

        tp_lock(tp);
        push_done_locked(tp, job);
        tp_unlock(tp);

        wakeup_main(tp);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ---- Create one worker thread ---- */

static void spawn_worker(MsThreadPool* tp, int idx) {
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, worker_fn, tp, 0, NULL);
    memcpy(tp->threads[idx], &h, sizeof(HANDLE));
#else
    pthread_t tid;
    pthread_create(&tid, NULL, worker_fn, tp);
    memcpy(tp->threads[idx], &tid, sizeof(pthread_t));
#endif
}

static void join_worker(MsThreadPool* tp, int idx) {
#ifdef _WIN32
    HANDLE h;
    memcpy(&h, tp->threads[idx], sizeof(HANDLE));
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
#else
    pthread_t tid;
    memcpy(&tid, tp->threads[idx], sizeof(pthread_t));
    pthread_join(tid, NULL);
#endif
    memset(tp->threads[idx], 0, sizeof(tp->threads[idx]));
}

/* ---- Free any jobs still in a queue (called during destroy) ---- */

static void free_job_queue(MsJob* head) {
    while (head) {
        MsJob* next = head->next;
        free(head->path);
        free(head->write_buf);
        free(head->result_buf);
        free(head);
        head = next;
    }
}

/* ---- Public API ---- */

void ms_threadpool_init(MsThreadPool* tp, int thread_count) {
    memset(tp, 0, sizeof(*tp));
    if (thread_count < 1) thread_count = 1;
    if (thread_count > 8) thread_count = 8;

    ms_mutex_t* m = (ms_mutex_t*)malloc(sizeof(ms_mutex_t));
    ms_cond_t*  c = (ms_cond_t*) malloc(sizeof(ms_cond_t));
    if (!m || !c) abort();
    tp_mutex_init(m);
    tp_cond_init(c);
    tp->mutex_opaque = m;
    tp->cond_opaque  = c;

#ifdef _WIN32
    tp->wakeup_event = (void*)CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!tp->wakeup_event) abort();
#else
    int fds[2];
    if (pipe(fds) != 0) abort();
    tp->wakeup_r = fds[0];
    tp->wakeup_w = fds[1];
#endif

    tp->thread_count = thread_count;
    for (int i = 0; i < thread_count; i++)
        spawn_worker(tp, i);
}

void ms_threadpool_destroy(MsThreadPool* tp) {
    tp_lock(tp);
    tp->shutdown = true;
    tp_bcast(tp);
    tp_unlock(tp);

    for (int i = 0; i < tp->thread_count; i++)
        join_worker(tp, i);
    tp->thread_count = 0;

    tp_mutex_destroy(get_mutex(tp));
    tp_cond_destroy(get_cond(tp));
    free(tp->mutex_opaque);
    free(tp->cond_opaque);
    tp->mutex_opaque = NULL;
    tp->cond_opaque  = NULL;

#ifdef _WIN32
    if (tp->wakeup_event) {
        CloseHandle((HANDLE)tp->wakeup_event);
        tp->wakeup_event = NULL;
    }
#else
    if (tp->wakeup_r >= 0) { close(tp->wakeup_r); tp->wakeup_r = -1; }
    if (tp->wakeup_w >= 0) { close(tp->wakeup_w); tp->wakeup_w = -1; }
#endif

    free_job_queue(tp->head);
    tp->head = tp->tail = NULL;
    free_job_queue(tp->done_head);
    tp->done_head = tp->done_tail = NULL;
}

void ms_threadpool_submit(MsThreadPool* tp, MsJob* job) {
    job->next = NULL;
    tp_lock(tp);
    if (!tp->tail) {
        tp->head = tp->tail = job;
    } else {
        tp->tail->next = job;
        tp->tail = job;
    }
    tp_signal(tp);
    tp_unlock(tp);
}

MsJob* ms_threadpool_pop_done(MsThreadPool* tp) {
    tp_lock(tp);
    MsJob* job = tp->done_head;
    if (job) {
        tp->done_head = job->next;
        if (!tp->done_head) tp->done_tail = NULL;
        job->next = NULL;
    }
    tp_unlock(tp);
    return job;
}
