# 标准库使用手册

> 本手册面向 **mslang 脚本作者**。如需查阅实现规格（C 代码、单测、依赖图），请参阅 [../index.md](../index.md)。

---

## 模块索引

| 模块 | 手册 | 一句话职责 |
|---|---|---|
| `math` | [math.md](math.md) | 纯数学函数与常量（无 IO）|
| `os` | [os.md](os.md) | 操作系统接口：进程、环境变量、文件系统、路径工具 |
| `time` | [time.md](time.md) | 时间获取、格式化解析、同步/异步睡眠、事件循环 |
| `io` | [io.md](io.md) | 文件 IO：整文件读写（同步 + 异步）、流式 File 句柄、标准流 |
| `buffer` | [buffer.md](buffer.md) | 可变字节缓冲区，用于二进制数据处理 |
| `hash` | [hash.md](hash.md) | MD5 / SHA1 / SHA256 / CRC32 / FNV1a（无外部依赖）|
| `log` | [log.md](log.md) | 分级日志，支持自定义 sink / 格式 / tag 子日志器 |
| `net` | [net.md](net.md) | 异步 TCP 网络：连接、监听、收发、DNS 解析 |
| `debug` | [debug.md](debug.md) | 运行时自省：调用栈、帧信息、字节码反汇编、类型检测 |
| `gc` | [gc.md](gc.md) | GC 手动控制与内存统计 |

独立示例脚本（可直接运行）：[`examples/`](examples/)

---

## 快速上手

### 导入模块

所有标准库模块都是**内置模块**，使用字符串名称导入：

```ms
import "math"
import "io"
import "os"
```

导入后通过模块名访问成员：

```ms
import "math"
print(math.sqrt(16))    // 4
print(math.PI)          // 3.14159...
```

也可以用 `from ... import` 只导入特定名称：

```ms
from "math" import sqrt, PI
print(sqrt(9))    // 3
print(PI)         // 3.14159...
```

### 第一个示例

```ms
import "math"
import "time"
import "os"

// 数学计算
print(math.pow(2, 10))    // 1024

// 格式化当前时间
print(time.format(time.now(), "%Y-%m-%d %H:%M:%S"))

// 获取当前工作目录
print(os.cwd())
```

### 运行 .ms 脚本

```bash
./build/Debug/mslang-c.exe path/to/script.ms
```

---

## 通用约定

### 输出

`print(...)` 是内置语句，支持任意数量参数，每次输出一行：

```ms
print("hello", "world")   // hello  world（空格分隔）
print(42, true, nil)
```

### 数值类型

| 类型 | 写法 | 说明 |
|---|---|---|
| `int` | `42`, `-1` | 64 位有符号整数 |
| `num` | `3.14`, `1e6` | 64 位双精度浮点 |

大多数 math 函数返回 `num`（包括 `floor` / `ceil`），如需 `int` 类型用 `int(math.floor(x))`。

### 错误处理

运行时错误会终止脚本并打印错误信息。可以用 `try/catch` 捕获：

```ms
try {
    import "io"
    io.read_file("不存在的文件")
} catch (e) {
    print("捕获到错误：" + e)
}
```

### 句柄对象

部分模块返回**句柄对象**（opaque handle），通过方法调用操作：

| 句柄类型 | 来源 | 主要方法 |
|---|---|---|
| `File` | `io.open(...)` | `read / readline / write / close / seek / tell` |
| `Buffer` | `buffer.new(...)` | `append / slice / to_str / to_hex / find` |
| `Hasher` | `hash.new("md5")` | 用 `hash.update` / `hash.hexdigest` 操作 |
| `Logger` | `log.with("tag")` | `info / warn / error / set_level` |
| `Socket` | `net.tcp_pair()` | `read / write / accept / close` |

### 异步模型

`net`、`io`（异步版本）和 `time.sleep` 返回 **Future**。Future 必须在 `async` 函数里 `await`，或由 `time.run_until_complete` 驱动事件循环：

```ms
import "time"

async fun main() {
    await time.sleep(10)    // 异步睡 10ms
    return "done"
}

var result = time.run_until_complete(main())
print(result)   // done
```

### 已知限制

- `print` 是关键字，**不能**作为值传递（如 `debug.is_native(print)` 会报错），请用 `len` / `str` 等普通 native 函数。
- map 字面量 `{"key": val}` 不能直接作为函数调用的第一个参数（解析歧义），请先赋给变量。
- `time.parse` 在 Windows 上仅支持 `%Y %m %d %H %M %S %j` 格式符。

---

## 示例脚本总览

| 文件 | 内容 |
|---|---|
| [`examples/math.ms`](examples/math.ms) | 取整、幂对数、三角、聚合、随机数 |
| [`examples/os.ms`](examples/os.ms) | 进程信息、文件系统操作、路径工具、stat |
| [`examples/time.ms`](examples/time.ms) | 时间戳获取、格式化、async 驱动 |
| [`examples/io.ms`](examples/io.ms) | 整文件读写、流式句柄、二进制 IO |
| [`examples/buffer.ms`](examples/buffer.ms) | 创建、追加、切片、hex 往返 |
| [`examples/hash.ms`](examples/hash.ms) | 一次性哈希、流式 Hasher、CRC32 |
| [`examples/log.ms`](examples/log.ms) | 分级日志、自定义格式、tag 子日志器 |
| [`examples/net.ms`](examples/net.ms) | tcp_pair 本地回环收发 |
| [`examples/debug.ms`](examples/debug.ms) | typeof、traceback、disasm、vm_stats |
| [`examples/gc.ms`](examples/gc.ms) | 统计信息、手动触发、暂停恢复 |
