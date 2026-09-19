# 08 并发（语言层）

go + channel 与 async/await 是**同一协程机制的两套语法**。调度器实现见 `24-scheduler.md`。

## 1. 总模型

```ms
go worker(1)                 // goroutine：并发执行单元，多线程 M:N 调度
var j = go slowTask()        // go 返回 Future：await j 即 join
async func fetch(url)        // async fn：协程函数，调用返回 Future
var r = await fetch(u)       // await：挂起直至完成
```

- 全部代码（含主模块体）运行在 goroutine 上；主模块体是主 goroutine。
- **多线程真并行**：worker 线程池默认数 = CPU 核数（`MS_SCHED_WORKERS` 可调）。
- 单 goroutine 内天然顺序执行；跨 goroutine 共享可变对象存在数据竞争，规则见 §8。

## 2. channel

```ms
var ch = make(chan)          // 无缓冲
var buf = make(chan, 8)      // 缓冲容量 8（int 容量，非类型参数——chan 无类型标注）
ch <- v                      // 发送：无缓冲时阻塞直至接收；缓冲满时阻塞
v = <-ch                     // 接收：空时阻塞；ch 关闭且空时立即返回 nil
<-ch                         // 接收并丢弃
close(ch)                    // 关闭：接收者排空后得到 nil
len(ch)                      // 当前缓冲元素数
cap(ch)                      // 容量
for v in ch { ... }          // 持续接收直到 close 且排空
```

- channel 是引用对象：赋值/传参共享同一 channel。
- **关闭语义**（Go 对齐）：向已关闭 channel 发送抛 ChannelError；重复 close 抛 ChannelError；接收已关闭且排空的 channel 得 nil（不阻塞不抛错）。
- nil channel：发送与接收**永久阻塞**；`close(nil)` 抛 ChannelError。
- 缓冲队列 FIFO；无缓冲发送/接收**同步会合**（rendezvous）。

## 3. select

```ms
select {
case v = <-ch1:
    print("got ${v}")
case ch2 <- x:
    print("sent")
case <-timeout:
    print("timeout")
default:
    print("nothing ready")
}
```

- 多 case 就绪时**均匀随机**选一；无就绪且无 default 则阻塞。
- case 体为块或单语句；v1 不支持 `select` 作为表达式、不支持带超时子句（用 timeout channel 惯用法）。

## 4. async / await

```ms
async func fetchAll(urls) {
    var futures = [fetch(u) for u in urls]      // 立即并发发起
    return await all(futures)                    // 并发等待（builtin all）
}

var page = await fetch("https://example.com")    // 顶层 await 合法：脚本本身在事件循环上
print(await time.sleep(0.5))                     // sleep 返回 Future，await 让出调度
```

- `async func` 调用**立即返回 Future**，函数体挂起执行至首个 await/完成。
- `await` 仅合法于 async 函数体与模块顶层；普通函数内报 SyntaxError。
- **顶层 await 合法**：入口脚本运行在事件循环上，顶层 await 阻塞后续顶层语句直至完成。
- Future：`.then(fn)` / `.catch(fn)` 链式回调；`await` 一个非 Future 值直接返回该值（容错）。
- async 函数内抛异常 → Future 携带异常，`await` 时重抛；未 await 且无 `.catch` 的失败 Future 在事件循环收尾时按 uncaught 处理。
- `all(fs)`：全部完成（任一失败立即失败）；`race(fs)`：首个完成。

## 5. go 与 async 的关系

- `go f(args)`：等价"发射后不管"的 goroutine；f 为 async 函数时同样合法（Future 无人 await 即并发执行）。
- `await` 在**任何** goroutine 内可用（一切代码皆协程）：await 即挂起点，不要求函数标记 async 之外的场景——但普通函数内仍禁止（静态约束，保持签名诚实）。
- 阻塞点全景：`await`、channel 收发阻塞、`time.sleep`、阻塞式 IO（脚本层调用阻塞 API 会将操作下抛线程池并挂起 goroutine，不占 worker，见 `24-scheduler.md` §6）。

## 6. 定时器

- `time.sleep(sec)` 返回 Future，由调度器定时器堆实现；精度毫秒级。
- `time.after(sec)` 返回超时 channel（select 惯用法）：`case <-time.after(1.0):`。

## 7. 取消

v1 显式取消仅两种途径：channel 关闭信号（惯用法）与 `runtime.cancel(fut)`（对 `go` 返回的 Future 抛 CancelledError 入目标协程；对 async Future 无效——async 协程由数据依赖自然终止）；`context` 风格封装在标准库 `sync` 模块（v1 后期，见 `26-stdlib.md`）。

## 8. 内存模型（重要）

- **字长读写原子**：单个 MsValue 槽（栈槽 / 数组元素 / map 槽）的读写不可撕裂；复合操作（`x += 1`、`m[k] += 1`）**不原子**。
- **数据竞争是脚本作者的责任**（Go 立场）：竞争不导致 VM 崩溃或 UB，但值结果未定义。需要同步时使用 `sync` 模块（Mutex / RWMutex / WaitGroup / Once / Cond）或 channel。
- GC 与调度器内部对所有共享结构的访问自有锁/无锁协议，脚本层不可见。

## 9. 死锁检测

全部 goroutine 睡眠且无可唤醒源（无定时器、无 IO 等待、无就绪 channel）时，调度器判定死锁，打印各 goroutine 栈并退出（退出码 2）。

## 10. 简明对照

| 需求 | 写法 |
|------|------|
| 后台执行 | `go f(x)` |
| 生产者/消费者 | channel + `for v in ch` |
| 限时等待 | `select { case v = <-ch: ... case <-time.after(1): }` |
| 并发收集 | `[fetch(u) for u in us]` + `await all(fs)` |
| 互斥 | `sync.Mutex`（见 `26-stdlib.md`） |
