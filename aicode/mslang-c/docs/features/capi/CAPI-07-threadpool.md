# CAPI-07: 文件异步线程池（Unix）

## 目标

Unix 上 epoll/kqueue 不支持普通文件描述符，`io.read_file_async` / `io.write_file_async` 需要借助线程池将阻塞 IO 搬离主线程。Windows IOCP 原生支持文件异步，走已有 reactor 路径，不需要本文件中的线程池。

---

## 架构

```
主线程（EventLoop）
    io.read_file_async(path)
        ├─ 创建 ObjFuture
        ├─ job 入 pool.queue（条件变量通知 worker）
        └─ 返回 Future（await 挂起协程）

Worker 线程（×4）
    取 job → 同步 fread → 结果写入 job.result
    通知主线程：Unix write(wakeup_fd, 1byte)

主线程 EventLoop（下一轮 poll）
    reactor 检测到 wakeup_fd 可读
    读出 1 byte → 消费 done_queue → ms_future_resolve(future, result)
    → 协程 wakeup → 继续执行
```

**关键约束**：GC 操作只在主线程执行。Worker 不操作 MsValue/MsObject，只操作原始 C 数据（`char*`、`size_t`）。主线程在 `ms_future_resolve` 时才把结果包装成 MsValue。

---

## 数据结构（`include/ms/threadpool.h`）

```c
#ifndef MS_THREADPOOL_H
#define MS_THREADPOOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    MS_JOB_READ_FILE,
    MS_JOB_WRITE_FILE,
} MsJobKind;

typedef struct MsJob {
    MsJobKind   kind;
    char*       path;       /* strdup，worker 完成后 free */
    char*       write_buf;  /* WRITE_FILE 时使用，strdup */
    size_t      write_len;
    /* 结果（worker 填写，主线程读取）*/
    char*       result_buf; /* READ_FILE 成功后的数据 */
    size_t      result_len;
    int         error;      /* errno，0 = 成功 */
    /* 关联的 Future 指针（主线程才可解引用）*/
    void*       future_opaque; /* 实际类型 MsObjFuture*，worker 不解引用 */
    struct MsJob* next;
} MsJob;

typedef struct {
    /* 工作队列（mutex + condvar）*/
    MsJob*      head;
    MsJob*      tail;
    /* 完成队列（与工作队列共用 mutex_opaque；push from worker, pop from main）*/
    MsJob*      done_head;
    MsJob*      done_tail;

    /* 线程 */
    void*       threads[8]; /* 最多 8 worker，默认 4 */
    int         thread_count;
    bool        shutdown;

    /* 平台唤醒机制 */
#ifdef _WIN32
    void*       iocp;       /* IOCP handle，挂在 reactor 上 */
#else
    int         wakeup_r;   /* pipe 读端 —— 挂到 epoll/kqueue */
    int         wakeup_w;   /* pipe 写端 —— worker 写 1 byte */
#endif

    /* 平台互斥 */
    void*       mutex_opaque;
    void*       cond_opaque;
} MsThreadPool;

void ms_threadpool_init(MsThreadPool* tp, int threads);
void ms_threadpool_destroy(MsThreadPool* tp);
void ms_threadpool_submit(MsThreadPool* tp, MsJob* job);

/* 供 EventLoop 一次性消费所有已完成的 job */
MsJob* ms_threadpool_pop_done(MsThreadPool* tp);
#endif
```

---

## Worker 循环（`src/threadpool.c`）

```c
static void* worker_fn(void* arg) {
    MsThreadPool* tp = (MsThreadPool*)arg;
    for (;;) {
        lock(tp); // pthread_mutex_lock
        while (!tp->head && !tp->shutdown)
            cond_wait(tp); // pthread_cond_wait
        if (tp->shutdown) { unlock(tp); break; }
        MsJob* job = tp->head;
        tp->head = job->next;
        if (!tp->head) tp->tail = NULL;
        unlock(tp);

        /* 执行阻塞 IO */
        if (job->kind == MS_JOB_READ_FILE) {
            FILE* f = fopen(job->path, "rb");
            if (!f) { job->error = errno; }
            else {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f); rewind(f);
                job->result_buf = malloc((size_t)sz + 1);
                job->result_len = fread(job->result_buf, 1, (size_t)sz, f);
                job->result_buf[job->result_len] = '\0';
                fclose(f);
            }
        } else { /* WRITE_FILE */
            FILE* f = fopen(job->path, "wb");
            if (!f) { job->error = errno; }
            else { fwrite(job->write_buf, 1, job->write_len, f); fclose(f); }
        }

        /* 推入完成队列 + 唤醒主线程 */
        push_done(tp, job);    // done_head/tail（持 mutex_opaque 加锁）
        wakeup_main(tp);       // Unix: write(wakeup_w, "\x01", 1)
    }
    return NULL;
}
```

---

## EventLoop 集成

在 `ms_loop_init` 时（EventLoop 已有 Reactor）：

1. `ms_threadpool_init(&vm->threadpool, 4)`
2. Unix：`ms_reactor_register(&vm->event_loop.reactor, tp->wakeup_r, MS_IO_READABLE, on_wakeup_cb)`

```c
static void on_wakeup_cb(MsVM* vm, int fd, MsIOEvent ev) {
    (void)fd; (void)ev;
    char buf[64]; read(vm->threadpool.wakeup_r, buf, sizeof(buf));  // 清空 pipe

    MsJob* job;
    while ((job = ms_threadpool_pop_done(&vm->threadpool))) {
        MsObjFuture* fut = (MsObjFuture*)job->future_opaque;
        if (job->error) {
            char errmsg[128];
            snprintf(errmsg, sizeof(errmsg), "IO error %d: %s",
                     job->error, strerror(job->error));
            MsValue err = MS_OBJ_VAL(ms_obj_string_copy(vm, errmsg, (int)strlen(errmsg)));
            ms_future_reject(vm, fut, err);
        } else {
            MsValue result;
            if (job->kind == MS_JOB_READ_FILE) {
                result = MS_OBJ_VAL(ms_obj_string_copy(vm,
                    job->result_buf, (int)job->result_len));
                free(job->result_buf);
            } else {
                result = MS_NIL_VAL();
            }
            ms_future_resolve(vm, fut, result);
        }
        ms_vm_unpin_future(vm, fut);  /* remove from pending_futures root set */
        free(job->path);
        free(job->write_buf);
        free(job);
    }
}
```

---

## `io.read_file_async` 实现

```c
static MsValue ms_io_read_file_async(MsVM* vm, int argc, MsValue* argv) {
    const char* path = MS_AS_CSTRING(argv[0]);
    MsObjFuture* fut = ms_obj_future_new(vm);
    /* Pin future as GC root until worker completes; prevents collection
       if the script drops the awaitable before the worker finishes. */
    ms_vm_pin_future(vm, fut);   /* adds fut to vm->pending_futures root set */

    MsJob* job = calloc(1, sizeof(MsJob));
    job->kind           = MS_JOB_READ_FILE;
    job->path           = strdup(path);
    job->future_opaque  = fut;

    ms_threadpool_submit(&vm->threadpool, job);
    return MS_OBJ_VAL(fut);
}
```

---

## Windows 路径

**v1**：Windows 与 Unix 实现相同——均走线程池阻塞 C 库（`fread`/`fwrite`）。区别仅在唤醒机制：以 `CreateEvent` / `SetEvent` 替代 Unix 的 `pipe(wakeup_r, wakeup_w)`，将事件句柄注册到 IOCP reactor 上。

条件编译：`#ifdef _WIN32 ... #else ... #endif`

OVERLAPPED/IOCP 直接文件异步路径留 v2 优化。

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `include/ms/threadpool.h` | 新建 |
| `include/ms/vm.h` | `MsVM` 增 `threadpool: MsThreadPool`（非指针，内嵌）|
| `src/threadpool.c` | 新建；pthread / Win32 实现 |
| `src/event_loop.c` | `ms_loop_init` 中初始化线程池；注册 wakeup_fd |
| `src/stdlib/io.c` | `ms_io_read_file_async`、`ms_io_write_file_async` |
| `CMakeLists.txt` | 加 `src/threadpool.c`；Unix 链接 `Threads::Threads`（find_package(Threads)）|

---

## v1 不做

- Windows OVERLAPPED 文件路径（留给后续，已改为阻塞线程池）
- 动态调整 worker 数量（可读 `MSLANG_IO_THREADS` 环境变量，v1 硬编码 4）
- Job 取消
- 超时
