# 内置标准库索引

> 前置依赖：[CAPI-01..08](../capi/) · [ASYNC-04..06](../async/)

| 状态 | 文件 | 模块 | 说明 |
|---|---|---|---|
| ⬜ | [STDLIB-00-overview.md](STDLIB-00-overview.md) | — | 总体架构、依赖图、ms_stdlib_register_all |
| ⬜ | [STDLIB-01-math.md](STDLIB-01-math.md) | `math` | 数学函数与常量 |
| ⬜ | [STDLIB-02-os.md](STDLIB-02-os.md) | `os` | 操作系统接口（env/fs/proc）|
| ⬜ | [STDLIB-03-time.md](STDLIB-03-time.md) | `time` | 时间、睡眠、async 调度（迁全局）|
| ⬜ | [STDLIB-04-io.md](STDLIB-04-io.md) | `io` | 文件 IO（同步 + 异步）+ ObjFile |
| ⬜ | [STDLIB-05-buffer.md](STDLIB-05-buffer.md) | `buffer` | 可变字节缓冲 ObjBuffer |
| ⬜ | [STDLIB-06-hash.md](STDLIB-06-hash.md) | `hash` | MD5/SHA/CRC32/FNV（无外部依赖）|
| ⬜ | [STDLIB-07-log.md](STDLIB-07-log.md) | `log` | 分级日志 + sink + tag |
| ⬜ | [STDLIB-08-net.md](STDLIB-08-net.md) | `net` | TCP（迁全局）+ DNS resolve |
| ⬜ | [STDLIB-09-debug.md](STDLIB-09-debug.md) | `debug` | 调用栈、反汇编、局部变量检查 |
| ⬜ | [STDLIB-10-gc.md](STDLIB-10-gc.md) | `gc` | GC 手动控制与统计 |

> ⬜ 待实现 · 🚧 进行中 · ✅ 完成

> **注**：标签编号（STDLIB-01..10）表示功能序号，不等于实施顺序。实施时须按依赖 DAG 排序：buffer → math → os → time → io → log → hash → net → debug → gc。

## 实施顺序

```
CAPI-01/02 (框架) → STDLIB-05 (buffer) → STDLIB-01/02/03 → CAPI-08 (全局迁移)
    → STDLIB-04 (io, 依赖 buffer + ObjFile) → CAPI-07 (线程池) → STDLIB-08 (net)
    → STDLIB-06/07/09/10 → CAPI-03/04 (动态加载)
```

## 前置依赖

| 依赖 | 说明 |
|---|---|
| CAPI-01（注册表）| 所有模块的入口机制 |
| CAPI-02（NativeDef）| 所有模块的函数注册方式 |
| CAPI-06（ObjFile/Buffer/Userdata）| io / buffer / hash 需要句柄类型 |
| ASYNC-04/05/06（EventLoop/Reactor/Socket）| net / time 迁移和异步 io |
