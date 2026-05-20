#pragma once
/* threadpool.h - CAPI-07: Async file IO thread pool.
 *
 * Workers execute blocking fread/fwrite and push completed MsJob entries
 * onto the done queue.  The main EventLoop drains the done queue when the
 * wakeup handle/fd fires, then wraps raw results into MsValues and resolves
 * the associated futures.
 *
 * GC constraint: workers never touch MsValue/MsObject.  Only the main
 * thread (in drain_threadpool) calls ms_future_resolve / ms_obj_string_copy.
 */
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    MS_JOB_READ_FILE,
    MS_JOB_WRITE_FILE,
} MsJobKind;

typedef struct MsJob {
    MsJobKind     kind;
    char*         path;          /* strdup; worker frees after done */
    char*         write_buf;     /* WRITE_FILE only; malloc'd copy   */
    size_t        write_len;
    /* result fields filled by worker */
    char*         result_buf;    /* READ_FILE: malloc'd data + '\0'  */
    size_t        result_len;
    int           error;         /* 0 = success, else errno          */
    /* opaque future pointer - main thread only */
    void*         future_opaque; /* actual type: MsObjFuture*        */
    struct MsJob* next;
} MsJob;

typedef struct {
    /* work queue (guarded by mutex_opaque) */
    MsJob* head;
    MsJob* tail;
    /* done queue (same mutex) */
    MsJob* done_head;
    MsJob* done_tail;

    /* worker threads (up to 8).
     * Stored as raw bytes to avoid platform headers in this public header.
     * sizeof(pthread_t) <= 8 on all supported platforms; HANDLE is <= 8 bytes.
     * threadpool.c casts via memcpy using the platform-specific thread type. */
    unsigned char threads[8][8]; /* 8 bytes per slot: fits pthread_t and HANDLE */
    int    thread_count;
    bool   shutdown;

    /* platform wakeup */
#ifdef _WIN32
    void*  wakeup_event; /* HANDLE from CreateEvent (manual-reset) */
#else
    int    wakeup_r;     /* pipe read end  - registered with reactor */
    int    wakeup_w;     /* pipe write end - worker writes 1 byte    */
#endif

    /* platform sync primitives (heap-allocated to avoid platform headers here) */
    void*  mutex_opaque;
    void*  cond_opaque;
} MsThreadPool;

/* Lifecycle */
void   ms_threadpool_init   (MsThreadPool* tp, int thread_count);
void   ms_threadpool_destroy(MsThreadPool* tp);

/* Submit a job to the work queue (thread-safe). */
void   ms_threadpool_submit (MsThreadPool* tp, MsJob* job);

/* Pop one completed job from the done queue (thread-safe).
   Returns NULL when the done queue is empty. */
MsJob* ms_threadpool_pop_done(MsThreadPool* tp);
