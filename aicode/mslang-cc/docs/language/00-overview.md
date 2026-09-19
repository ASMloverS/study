# mslang 语言概览

> mslang = Maple Scripting Language。源文件后缀 `.ms`，字节码缓存 `.msc`，打包产物 `.msb`。

## 1. 定位

mslang 是一门**动态类型脚本语言**：

- **语法外观取 Go**：`func / var / const / :=`，无分号（ASI），条件无括号，块必有花括号。
- **运行语义取 Python**：一切皆对象、class + 继承、异常机制、文件模块、batteries-included 标准库。
- **实现架构取 CPython**：源码 → AST → 栈式字节码 → VM 执行；纯 C11 实现。
- **并发取 Go**：多线程 M:N 调度器 + goroutine + channel，另设 async/await 语法糖。
- **GC 取 Go**：分代 + 并发三色标记清除。

标准库长期目标：对齐 Go std 与 Python std 的模块规模并做融合映射（见 `26-stdlib.md`）。

## 2. 设计原则

| 原则 | 含义 |
|------|------|
| 一眼看懂 | Go 程序员零成本上手语法，Python 程序员零成本上手语义 |
| 脚本优先 | 顶层即入口、顶层 await 合法、单文件可运行 |
| 无歧义 | 只有一种做事情的方式；`ms fmt` 输出唯一风格 |
| 嵌入友好 | ms.h 单头稳定 ABI；追踪式 GC 下用 handle rooting，C 扩展安全 |
| 可验证 | 每个特性有 conformance 测试；性能有基准门禁 |

## 3. 一段完整示例

```ms
// fib.ms — 顶层即入口，顶层 await 合法
import "strings" as str
import "time"

const GREETING = "Hello, mslang!"

func fib(n) {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}

class Greeter {
    func __init__(name) { this.name = name }
    func greet() -> "${GREETING} ${this.name}, fib(10) = ${fib(10)}"
}

func main() {
    var g = Greeter("world")
    print(g.greet())                          // Hello, mslang! world, fib(10) = 55
    print([x * x for x in range(1, 6)])       // [1, 4, 9, 16, 25]
    print(str.toUpper("mslang"))              // MSLANG

    var results = make(chan, 4)
    for i in range(4) {
        go func() { results <- fib(i * 5) }()
    }
    var total = 0
    for _ in range(4) { total += <-results }
    print("total = ${total}")
    await time.sleep(0.1)
}
```

运行：`ms fib.ms`。

## 4. 特性清单（v1）

- 动态类型；`nil / bool / int / bigint / float / string / list / map / tuple / set / function / class / instance`
- int 64 位，溢出自动升 bigint；int/float/bigint 混算规则见 `02-types.md`
- 不可变字符串，UTF-8 字节存储，rune 索引，`${}` 插值
- class：单继承 + mixin、dunder 魔法方法、运算符重载、动态字段
- 异常：`throw / try / catch / finally` + `defer`（LIFO）
- 并发：`go` + channel（`select`）+ `async func / await`，多线程 M:N 调度
- 模块：Python 式文件模块，`import` / `from ... import { }`，下划线私有
- 推导式、切片表达式、三元、解构赋值、生成器（`func*` + `yield`）
- 全套工具链：`ms run / repl / fmt / test / build / pkg / doc / dump / debug`

## 5. 非目标（v1 明确不做）

- JIT 编译（预留 quickening 优化即可）
- 静态类型检查 / 强制类型标注
- `__del__` 终结器（用 defer + WeakRef 覆盖）
- 反射 API（`type()` 与 dunder 协议足够 v1）
- LSP server

## 6. 术语

| 术语 | 含义 |
|------|------|
| rune | Unicode 码点（int 子值），`'a'` 字面量 |
| chunk | 一个函数/模块的字节码编译单元 |
| goroutine | `go` 启动的轻量协程，M:N 调度 |
| Future | async func 的返回句柄，可 await |
| dunder | `__xxx__` 形式的魔法方法 |
| handle | C API 中指向 GC 对象的间接引用（稳定跨 GC） |
| Tier 0/1/2/3 | 标准库分层：内置 / v1 必备 / v1 后期 / v2+ |

## 7. 文档地图

- 语言层：`01` 词法 → `02` 类型 → `03` 表达式 → `04` 语句 → `05` 函数 → `06` 类 → `07` 异常 → `08` 并发 → `09` 模块 → `10` 内置 → `11` ms 编码规范
- 实现层：`20` 总体架构 → `21` 字节码 → `22` 对象模型 → `23` GC → `24` 调度器 → `25` C API
- 工程层：`26` 标准库 → `27` 工具链 → `28` C 编码规范 → `29` 路线图
