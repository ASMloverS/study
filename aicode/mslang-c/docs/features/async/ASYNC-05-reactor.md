# ASYNC-05: 跨平台 Reactor（IO 多路复用）

## 职责

Reactor 封装操作系统的 IO 就绪通知机制，向 EventLoop 提供统一接口：

- **Linux**：`epoll`
- **macOS / BSD**：`kqueue`
- **Windows**：`IOCP`（I/O Completion Ports）

EventLoop 调用 `ms_reactor_poll(reactor, timeout_ms)`，Reactor 返回就绪事件，  
EventLoop 将对应的 future resolve，触发 waiter 协程进就绪队列。

## 统一接口（`include/ms/reactor.h`）

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

// IO 事件类型
typedef enum {
    MS_IO_READABLE = 1 << 0,
    MS_IO_WRITABLE = 1 << 1,
    MS_IO_ERROR    = 1 << 2,
} MsIOEvent;

// 就绪事件（poll 返回）
typedef struct {
    int fd;               // 触发的文件描述符（POSIX）或 uintptr_t (IOCP)
    MsIOEvent events;
    void* user_data;      // 注册时传入的回调数据（通常是 MsObjFuture* 或 callback）
} MsIOReadyEvent;

// Reactor 句柄（不透明，各平台内部实现）
typedef struct MsReactor MsReactor;

// 生命周期
int  ms_reactor_init(MsReactor* r);
void ms_reactor_destroy(MsReactor* r);

// 注册 / 修改 / 注销 fd 的监听事件
int ms_reactor_register(MsReactor* r, int fd, MsIOEvent events, void* user_data);
int ms_reactor_modify(MsReactor* r, int fd, MsIOEvent events, void* user_data);
int ms_reactor_unregister(MsReactor* r, int fd);

// 等待 IO 就绪（阻塞最多 timeout_ms；0 = 立即返回；-1 = 永久等待）
// 就绪事件写入 out_events[0..out_count)
int ms_reactor_poll(MsReactor* r, int64_t timeout_ms,
                    MsIOReadyEvent* out_events, int max_events, int* out_count);
```

## 平台实现

### Linux — epoll（`src/reactor_epoll.c`）

```c
struct MsReactor {
    int epfd;  // epoll fd
};

int ms_reactor_init(MsReactor* r) {
    r->epfd = epoll_create1(EPOLL_CLOEXEC);
    return r->epfd >= 0 ? 0 : -1;
}

int ms_reactor_register(MsReactor* r, int fd, MsIOEvent events, void* user_data) {
    struct epoll_event ev = {0};
    if (events & MS_IO_READABLE) ev.events |= EPOLLIN;
    if (events & MS_IO_WRITABLE) ev.events |= EPOLLOUT;
    ev.events |= EPOLLET;  // 边沿触发
    ev.data.ptr = user_data;
    return epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev);
}

int ms_reactor_poll(MsReactor* r, int64_t timeout_ms,
                    MsIOReadyEvent* out, int max, int* count) {
    struct epoll_event evs[max];
    int n = epoll_wait(r->epfd, evs, max, (int)timeout_ms);
    if (n < 0) { *count = 0; return -1; }
    for (int i = 0; i < n; i++) {
        out[i].fd        = -1;  // epoll edge: fd 从 user_data 取
        out[i].user_data = evs[i].data.ptr;
        out[i].events    = 0;
        if (evs[i].events & EPOLLIN)  out[i].events |= MS_IO_READABLE;
        if (evs[i].events & EPOLLOUT) out[i].events |= MS_IO_WRITABLE;
        if (evs[i].events & EPOLLERR) out[i].events |= MS_IO_ERROR;
    }
    *count = n;
    return 0;
}
```

### macOS / BSD — kqueue（`src/reactor_kqueue.c`）

```c
struct MsReactor { int kqfd; };

// kqueue kevent 注册 EVFILT_READ / EVFILT_WRITE，udata = user_data
// kevent() 返回就绪事件
```

### Windows — IOCP（`src/reactor_iocp.c`）

```c
struct MsReactor { HANDLE iocp; };
// CreateIoCompletionPort + GetQueuedCompletionStatusEx
// 每个 IO 操作绑定一个 OVERLAPPED，完成时从 IOCP 取出
// user_data 藏在 OVERLAPPED 扩展结构的 first 字段
```

> IOCP 与 epoll/kqueue 的语义差异：IOCP 是完成通知（IO 已完成），  
> epoll/kqueue 是就绪通知（IO 可以开始）。  
> 统一接口时在 `reactor_iocp.c` 内部把 IOCP 完成事件映射为"可读/可写就绪"，  
> socket 层用 `WSARecv`/`WSASend` + overlapped 预投递，完成即触发。

## CMake 平台选择（`CMakeLists.txt`）

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(mslang-c PRIVATE src/reactor_epoll.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME MATCHES "BSD")
    target_sources(mslang-c PRIVATE src/reactor_kqueue.c)
elseif(WIN32)
    target_sources(mslang-c PRIVATE src/reactor_iocp.c)
    target_link_libraries(mslang-c PRIVATE ws2_32)
else()
    message(FATAL_ERROR "Unsupported platform for async reactor")
endif()
```

## EventLoop 与 Reactor 的交互

在 `ms_loop_run_until_complete` 的步骤 4（`ASYNC-04`）中：

```c
MsIOReadyEvent events[64];
int count = 0;
ms_reactor_poll(&loop->reactor, timeout_ms, events, 64, &count);
for (int i = 0; i < count; i++) {
    // user_data 是一个回调结构 { future_read, future_write }
    MsIOCallback* cb = (MsIOCallback*)events[i].user_data;
    if ((events[i].events & MS_IO_READABLE) && cb->read_future)
        ms_future_resolve(loop->vm, cb->read_future, MS_NIL_VAL());
    if ((events[i].events & MS_IO_WRITABLE) && cb->write_future)
        ms_future_resolve(loop->vm, cb->write_future, MS_NIL_VAL());
    if (events[i].events & MS_IO_ERROR)
        ms_future_reject(loop->vm, cb->read_future ?: cb->write_future, /* err */);
}
```

## 验证

```
tests/unit/test_reactor.c：
  - 自管道（pipe）self-trigger 测试：
    1. 创建 pipe(r_fd, w_fd)
    2. reactor 注册 r_fd 读监听
    3. write(w_fd, "x", 1)
    4. reactor_poll(timeout=100) → 返回 r_fd 可读事件
    5. 再注销 r_fd，cleanup
  - 超时测试：没有事件时 poll 在 timeout_ms 后返回 count=0
```

测试用 `pipe()`（POSIX）/ `CreatePipe()`（Windows）替代真实 socket，  
保持单元测试无网络依赖。
