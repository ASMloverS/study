# ASYNC-05: 跨平台 Reactor（IO 多路复用）

## 职责

Reactor 封装操作系统的 IO 就绪通知机制，向 EventLoop 提供统一接口：

- **Linux**：`epoll`
- **macOS / BSD**：`kqueue`
- **Windows**：`IOCP`（I/O Completion Ports）

EventLoop 调用 `ms_reactor_poll(reactor, timeout_ms)`，Reactor 返回就绪事件，  
EventLoop 将对应的 future resolve，触发 waiter 协程进就绪队列。

## 两层语义并存

epoll / kqueue 是**就绪通知**（readiness）：fd 可读/可写时通知，应用自行 syscall。  
IOCP 是**完成通知**（completion）：必须预先投递缓冲区，IO 完成后收到事件。

两者不能共用一套接口，因此 Reactor 分两层 API：

| 层 | 函数 | 适用平台 |
|---|---|---|
| Readiness | `ms_reactor_arm` | epoll / kqueue |
| Completion | `ms_reactor_submit_read` / `ms_reactor_submit_write` | IOCP（也可在 epoll 上内部模拟） |

POSIX 平台：`socket.read` 调用 `ms_reactor_arm(fd, READABLE)` 注册，就绪后执行 `recv`。  
Windows 平台：`socket.read` 调用 `ms_reactor_submit_read(fd, buf, len)`，IOCP 完成时直接携带数据。

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

// === Readiness-style（epoll / kqueue 直接映射）===
// arm：与 register 等价，fd 就绪时产生事件，应用再调 recv/send
int ms_reactor_arm(MsReactor* r, int fd, MsIOEvent events, void* user_data);

// === Completion-style（IOCP 必需；epoll 可内部模拟）===
// 预投递 read/write 缓冲区；完成时事件携带实际传输字节数
int ms_reactor_submit_read(MsReactor* r, int fd, void* buf, int len, void* user_data);
int ms_reactor_submit_write(MsReactor* r, int fd, const void* buf, int len, void* user_data);

// 等待 IO 就绪/完成（阻塞最多 timeout_ms；0 = 立即返回；-1 = 永久等待）
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
    // 使用水平触发（LT，默认）而非边沿触发（ET）：
    // ET 要求循环读至 EAGAIN，单次 recv 会丢失剩余数据；LT 更安全，性能差异可接受。
    ev.data.ptr = user_data;
    return epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev);
}

int ms_reactor_unregister(MsReactor* r, int fd) {
    return epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, NULL);
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

// 注册：EV_SET + kevent(kqfd, &kev, 1, NULL, 0, NULL)
// 注销：EV_SET(kev, fd, EVFILT_READ|EVFILT_WRITE, EV_DELETE, ...)
int ms_reactor_unregister(MsReactor* r, int fd) {
    struct kevent kev[2];
    EV_SET(&kev[0], fd, EVFILT_READ,  EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    return kevent(r->kqfd, kev, 2, NULL, 0, NULL);
}
// kevent() 返回就绪事件
```

### Windows — IOCP（`src/reactor_iocp.c`）

```c
struct MsReactor { HANDLE iocp; };
// CreateIoCompletionPort + GetQueuedCompletionStatusEx
// 每个 IO 操作绑定一个 OVERLAPPED，完成时从 IOCP 取出
// user_data 藏在 OVERLAPPED 扩展结构的 first 字段
```

> IOCP 完成通知要求调用方在操作前预先投递缓冲区（`WSARecv`/`WSASend` + OVERLAPPED）；  
> 完成后 `GetQueuedCompletionStatusEx` 返回，携带实际传输字节数。  
> POSIX 路径调 `ms_reactor_arm`，Windows 路径调 `ms_reactor_submit_read/write`；  
> `reactor_iocp.c` **不**尝试把完成事件伪装成就绪事件，两路径语义清晰分离。

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
