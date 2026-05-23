# STDLIB-41: sync 模块

## 职责

协程安全同步原语：Once（单次执行）、WaitGroup（等待多个协程）、Channel（协程间通信）。
基于 mslang 单线程 coroutine/async 模型，通过 Future 实现非阻塞等待。
对应 Go 的 `sync` 包（无 Mutex 死锁风险——单线程无数据竞争）。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/sync.ms`）。基于 `time` 模块的 async 调度（`time.run_until_complete`）。

---

## 函数与类型清单

### Once — 仅执行一次

```ms
var o = sync.new_once(fn)
o.do()   // 只有第一次调用 fn；后续调用无效
o.done() // bool：fn 是否已执行
```

### WaitGroup — 等待多个任务

```ms
var wg = sync.new_wait_group()
wg.add(n)    // 增加计数 n
wg.done()    // 减少计数 1
// await wg.wait()  → Future，计数归零时 resolve
```

### Channel — 协程间通信

```ms
var ch = sync.new_channel(capacity=0)  // 0=无缓冲，>0=有缓冲
// await ch.send(v)  → Future
// await ch.recv()   → Future<any>
ch.close()           // 关闭 channel
ch.is_closed() → bool
ch.len()       → int  (当前缓冲中的元素数)
```

### 辅助

| 函数 | 描述 |
|---|---|
| `sync.gather(futures)` | 等待所有 Future 完成，返回结果列表（委托 `time.gather`）|
| `sync.race(futures)` | 返回第一个 resolve 的 Future 结果（`time` 无对应原语，由 sync 基于 EventLoop 自行实现）|

---

## 依赖

- `time` 模块（STDLIB-03 ✅，`Future`/`run_until_complete`/`gather`）
- async 运行时（ASYNC-04/05/06）

---

## 示例

```ms
import "sync"
import "time"

// WaitGroup
async fun worker(id, wg) {
    await time.sleep(10)
    print("worker " + str(id) + " done")
    wg.done()
}

async fun main() {
    var wg = sync.new_wait_group()
    wg.add(3)
    for i in [1,2,3] { worker(i, wg) }
    await wg.wait()
    print("all done")
}
time.run_until_complete(main())

// Channel
async fun producer(ch) {
    for i in [1,2,3] { await ch.send(i) }
    ch.close()
}
async fun consumer(ch) {
    while !ch.is_closed() {
        var v = await ch.recv()
        if v != nil { print(v) }
    }
}
```

---

## 测试

```
tests/unit/test_stdlib_sync.c
tests/fixtures/stdlib_sync_basic.ms
```
