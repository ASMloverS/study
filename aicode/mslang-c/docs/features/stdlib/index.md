# 内置标准库索引

> **用户手册（脚本示例）**：[manual/index.md](manual/index.md)

> 前置依赖：[CAPI-01..08](../capi/) · [ASYNC-04..06](../async/)

| 状态 | 文件 | 模块 | 说明 |
|---|---|---|---|
| ✅ | [STDLIB-00-overview.md](STDLIB-00-overview.md) | — | 总体架构、依赖图、ms_stdlib_register_all |
| ✅ | [STDLIB-01-math.md](STDLIB-01-math.md) | `math` | 数学函数与常量 |
| ✅ | [STDLIB-02-os.md](STDLIB-02-os.md) | `os` | 操作系统接口（env/fs/proc）|
| ✅ | [STDLIB-03-time.md](STDLIB-03-time.md) | `time` | 时间、睡眠、async 调度（迁全局）|
| ✅ | [STDLIB-04-io.md](STDLIB-04-io.md) | `io` | 文件 IO（同步 + 异步）+ ObjFile |
| ✅ | [STDLIB-05-buffer.md](STDLIB-05-buffer.md) | `buffer` | 可变字节缓冲 ObjBuffer |
| ✅ | [STDLIB-06-hash.md](STDLIB-06-hash.md) | `hash` | MD5/SHA/CRC32/FNV（无外部依赖）|
| ✅ | [STDLIB-07-log.md](STDLIB-07-log.md) | `log` | 分级日志 + sink + tag |
| ✅ | [STDLIB-08-net.md](STDLIB-08-net.md) | `net` | TCP（迁全局）+ DNS resolve |
| ✅ | [STDLIB-09-debug.md](STDLIB-09-debug.md) | `debug` | 调用栈、反汇编、局部变量检查 |
| ✅ | [STDLIB-10-gc.md](STDLIB-10-gc.md) | `gc` | GC 手动控制与统计 |

> ⬜ 待实现 · 📐 已设计（待实现）· 🚧 进行中 · ✅ 完成

> **注**：标签编号（STDLIB-01..10）表示功能序号，不等于实施顺序。实施时须按依赖 DAG 排序：buffer → math → os → time → io → log → hash → net → debug → gc。

---

## Go std 对标扩展（STDLIB-11+）

> 完整映射与分层架构：[STDLIB-11](STDLIB-11-go-mapping.md) · [STDLIB-12](STDLIB-12-ms-tier-architecture.md)

### Tier 1 — 核心高频（C 原语 + .ms 组合层）

| 状态 | 文件 | 模块 | 说明 |
|---|---|---|---|
| ✅ | [STDLIB-13](STDLIB-13-strings.md) | `strings` | 字符串工具函数（join/repeat/fields/字符类）|
| ✅ | [STDLIB-14](STDLIB-14-strconv.md) | `strconv` | 字符串↔基本类型转换 |
| ✅ | [STDLIB-15](STDLIB-15-fmt.md) | `fmt` | sprintf/printf/格式化 |
| ✅ | [STDLIB-16](STDLIB-16-json.md) | `json` | JSON 编解码（纯 C）|
| ✅ | [STDLIB-17](STDLIB-17-errors.md) | `errors` | 结构化错误值与链式检查 |
| ✅ | [STDLIB-18](STDLIB-18-slices.md) | `slices` | list 工具（map/filter/reduce/…）|
| ✅ | [STDLIB-19](STDLIB-19-maps.md) | `maps` | map 工具（keys/values/merge/…）|
| ✅ | [STDLIB-20](STDLIB-20-sort.md) | `sort` | 排序与二分搜索 |
| ✅ | [STDLIB-21](STDLIB-21-set.md) | `set` | 集合（并/交/差）|
| ✅ | [STDLIB-22](STDLIB-22-heap.md) | `heap` | 优先队列 |
| ✅ | [STDLIB-23](STDLIB-23-itertools.md) | `itertools` | 惰性迭代器（基于 generator）|
| ✅ | [STDLIB-24](STDLIB-24-random.md) | `random` | 高质量 PRNG + 分布操作 |
| ✅ | [STDLIB-25](STDLIB-25-base64.md) | `base64` | Base64 编解码（纯 C）|
| ✅ | [STDLIB-26](STDLIB-26-hex.md) | `hex` | 十六进制编解码 |
| ✅ | [STDLIB-27](STDLIB-27-bytes.md) | `bytes` | 字节操作（基于 buffer）|
| ✅ | [STDLIB-28](STDLIB-28-regexp.md) | `regexp` | 正则引擎（NFA，纯 C）|
| ✅ | [STDLIB-29](STDLIB-29-path.md) | `path` | 路径字符串工具 |
| ✅ | [STDLIB-30](STDLIB-30-bufio.md) | `bufio` | 缓冲 IO（基于 io + buffer）|

### Tier 2 — 广泛扩展

| 状态 | 文件 | 模块 | 说明 |
|---|---|---|---|
| ✅ | [STDLIB-31](STDLIB-31-unicode.md) | `unicode` | Unicode 属性/UTF-8 编解码（纯 C）|
| ✅ | [STDLIB-32](STDLIB-32-binary.md) | `binary` | 大小端打包/varint（纯 C）|
| ✅ | [STDLIB-33](STDLIB-33-csv.md) | `csv` | CSV 读写 |
| ✅ | [STDLIB-34](STDLIB-34-base32.md) | `base32` | Base32 编解码 |
| 📐 | [STDLIB-35](STDLIB-35-cmp.md) | `cmp` | 比较器组合器 |
| 📐 | [STDLIB-36](STDLIB-36-deque.md) | `deque` | 双端队列 |
| 📐 | [STDLIB-37](STDLIB-37-linkedlist.md) | `linkedlist` | 双向链表 |
| 📐 | [STDLIB-38](STDLIB-38-ring.md) | `ring` | 循环缓冲区 |
| 📐 | [STDLIB-39](STDLIB-39-flag.md) | `flag` | 命令行参数解析 |
| 📐 | [STDLIB-40](STDLIB-40-url.md) | `url` | URL 解析与编码 |
| 📐 | [STDLIB-41](STDLIB-41-sync.md) | `sync` | 协程同步原语（Once/WaitGroup/Channel）|
| 📐 | [STDLIB-42](STDLIB-42-context.md) | `context` | 请求上下文（取消/截止）|
| 📐 | [STDLIB-43](STDLIB-43-bits.md) | `bits` | 位操作（popcount/rotate/…，纯 C）|
| 📐 | [STDLIB-44](STDLIB-44-hmac.md) | `hmac` | HMAC（基于 hash 模块）|
| 📐 | [STDLIB-45](STDLIB-45-testing.md) | `testing` | 测试框架（assert/run/bench）|
| 📐 | [STDLIB-46](STDLIB-46-template.md) | `template` | 数据驱动文本模板 |

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
