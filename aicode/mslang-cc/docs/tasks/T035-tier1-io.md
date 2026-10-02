# T035 — Tier1 组 B：os/sys/io/bufio/bytes/path/collections/sync

- 阶段: P10 C API + Tier1（roadmap 期 10）
- 依赖: T032、T033
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/26-stdlib.md` §2（对应行）、`24-scheduler.md` §6（阻塞下抛）、`08-concurrency.md` §8（sync 语义）

## 1. 目标与范围

以 C 实现并注册 8 个 IO/系统 Tier1 模块：`os`、`sys`、`io`、`bufio`、`bytes`、`path`、`collections`、`sync`。阻塞型 API 走 bpool 下抛（G 挂起不占 worker）。

不做：`io` 文本层（文本处理用 strings，26 §2 明确二进制 IO）、`collections.defaultList` 之外的自定义容器类原生优化（脚本类实现即可者不再写 C）。

## 2. 实现要点

- 通用：文件/目录操作统一 plat_io 封装；错误映射 OSError→IOError/FileNotFoundError 族；阻塞调用包 `msBlockingCall(fn, arg)` 下抛 bpool。
- os（26 §2）：getenv/setenv/unsetenv/environ/args/exit/cwd/chdir/readFile/writeFile/listdir/mkdir/mkdirAll/remove/removeAll/rename/stat(isDir isFile size mtime)/isTerminal。
- sys（26 §2）：argv/path/executable/platform/version/stdin stdout stderr 句柄/exit；gc/memStats/goroutineStats 转发 runtime（T036）。
- io（26 §2）：open(path[, mode])（rb/wb/ab/r+/… 二进制模式串）/文件句柄 read/readLine/write/flush/seek/tell/close/copy(src,dst)/readAll；Reader/Writer 抽象（duck typing：read/write 方法约定，文档化）。
- bufio（26 §2）：newReader/newWriter（缓冲尺寸可选）/scanner 行分割/readString/readByte/writeString/writeByte/flush。
- bytes（26 §2）：Buffer 类型（可变字节缓冲；原生类最小注册——T033 桩升级）/fromString/append/extend/read/write/indexOf/contains/replace/toLower/toUpper/toString/len/cap/truncate。
- path（26 §2）：join/clean/abs/rel/base/dir/ext/split/exists/isDir/isFile/isAbs/expand（~ 与环境变量）/separator 常量——join/clean 按 POSIX 规则 + 平台分隔符适配（Windows 同时接受 / 与 \）。
- collections（26 §2）：deque（双端队列，原生类）/heapq（heappush/heappop/heapify/nlargest/nsmallest）/Counter/OrderedMap/defaultList/ring——Counter/OrderedMap 等以脚本类实现放入内置脚本库（标准库脚本源码分发位），deque/heapq 用 C。
- sync（26 §2）：Mutex/RWMutex/WaitGroup/Once/Cond（broadcast/signal/wait）/Chan 工具 merge fanIn/deadlockTimeout——Mutex 族 C 原生类（plat 封装）；锁语义按 08 §8（数据竞争是脚本作者责任，锁不重入：重入死锁由 deadlockTimeout 或死锁检测兜底）。
- time 模块已在 T032（本任务复核注册表完整性）。

## 3. 涉及文件

新增: `src/stdlib/os.c`、`src/stdlib/sys.c`、`src/stdlib/io.c`、`src/stdlib/bufio.c`、`src/stdlib/bytes.c`、`src/stdlib/path.c`、`src/stdlib/collections.c`、`src/stdlib/sync.c`、`stdlib_src/collections_ms/*.ms`（脚本实现的标准库源）、`tests/unit/test_stdlib_io.c`、`tests/fixtures/stdlib_os|sys|io|bufio|bytes|path|collections|sync/*.ms`
修改: `src/module/builtin_libs.c`（注册 + 脚本源库加载位）、`src/sched/bpool.c`（阻塞包装 API）、`CMakeLists.txt`（stdlib_src 安装路径）

## 4. 验收标准（DoD）

- [ ] 26 §2 八模块全函数可 import 且行为正确（逐函数勾选清单）
- [ ] io/bufio/bytes 读写往返：write→flush→read 全等（临时目录 fixture）
- [ ] 阻塞不占 worker：循环 `os.readFile` 期间并发 G 持续运行（观测输出交替）
- [ ] path join/clean Windows 与 POSIX 用例各自正确（平台自适应断言）
- [ ] sync.Mutex 临界区计数正确；WaitGroup 汇合；Once 单次；Cond 唤醒
- [ ] heapq/deque 复杂度行为正确（大 N 排序对照 sorted）
- [ ] exit 退出码透传（fixture `.code` 断言）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_stdlib_io.c`

| 用例 | 调用 | 期望 |
|------|------|------|
| 文件往返 | io.open 写读 | 字节全等 |
| seek/tell | 定位读写 | 偏移正确 |
| stat | 临时文件 | size/mtime 合理 |
| path.join | 多段拼接 | 平台规范化 |
| Buffer 增长 | 追加 1MB | indexOf/toString 正确 |
| bufio scanner | 多行文件 | 行序列正确 |
| Mutex 互斥 | 双 G 计数 | 无丢失更新 |

### 5.2 ms fixtures（节选）

- `stdlib_io/copy_file.ms`：

```ms
import "io"
var src = io.open("in.txt")
defer src.close()
io.copy(src, io.open("out.txt", "wb"))
print(io.open("out.txt").readAll().len())
```

- `stdlib_sync/mutex_counter.ms`：100 G 竞争累加 + WaitGroup 汇合（确定性总和）
- `stdlib_os/file_ops.ms`：mkdir/writeFile/readFile/listdir/removeAll 生命周期
- `stdlib_path/join_clean.ms`：join/clean/base/dir/ext 表用例
- `stdlib_collections/heapq.ms`：heappush/pop 与 sorted 对照

## 6. 风险与备注

- Windows 文件语义（removeAll 只读属性等）边界：以"尽力 + IOError 明确信息"策略，用例避开平台差异区。
- 脚本实现的标准库源（Counter/OrderedMap 等）走 09 §4 第 5 级标准库位分发：安装布局 `stdlib_src/` 随二进制发布（26 §1 源码随二进制分发）。
