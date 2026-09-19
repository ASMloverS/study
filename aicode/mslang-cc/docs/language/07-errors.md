# 07 异常与 defer

Python 语义的异常机制，Java 拼写的关键字（try / catch / finally / throw）。

## 1. 语法

```ms
try {
    risky(x)
} catch e {                              // 全捕获
    print("caught: ${repr(e)}")
} catch ValueError, TypeError {          // 按类型（instanceof 匹配）
    ...
} finally {
    cleanup()
}

throw ValueError("bad input: ${x}")      // throw 任意表达式；惯用 Error 子类实例
```

- catch 子句可多个，自上而下首个类型匹配者执行；`catch e`（裸绑定）须在类型子句之后，作为兜底。
- 类型子句的 `e` 绑定可选：`catch ValueError { }`（不需要异常对象时）。

## 2. 内置异常类树

```
Error                       一切异常的根
├── ValueError              值不合法
├── TypeError               类型不匹配
├── KeyError                map 键缺失
├── IndexError              索引越界
├── ArithmeticError         算术（ZeroDivisionError 之外的一般算术错误）
│   └── ZeroDivisionError   除零 / 模零
├── OverflowError           int → float 转换溢出等
├── ArgumentError          参数个数 / 形态不匹配
├── AttributeError          成员不存在
├── RuntimeError            运行时兜底
│   ├── StackOverflowError  协程栈超限
│   └── IteratorError       迭代协议违约
├── ImportError             模块加载失败 / 循环导入
├── IOError                 文件与 IO（含 FileNotFoundError）
│   └── FileNotFoundError
├── ChannelError            向已关闭 channel 发送 / 操作 nil channel
└── CancelledError          协程被取消
```

- 全部内置类型**无需 import**，语言核心直接提供；`errors` 标准库模块提供 `wrap / unwrap / is` 工具与自定义异常基类辅助（见 `26-stdlib.md`）。
- `e.msg`：异常消息；`e.str()` 默认 `"TypeName: msg"`。

## 3. 语义细则

- **finally 必然执行**：正常退出、捕获、再抛出、协程取消均触发；finally 中 `return` / `throw` 覆盖进行中的退出值（同 Python/Java，慎用）。
- 异常对象携带栈回溯（`e.trace`：帧列表，文件:行:函数）；**uncaught 异常**打印彩色 traceback（含源码行）并以退出码 1 结束。
- `throw` 在 catch 块内裸写（`throw` 不带表达式）表示**重抛当前异常**，保留原 traceback。
- 异常跨 goroutine 边界**不传播**：goroutine 内 uncaught 异常仅终止该 goroutine，打印 traceback；主协程异常终止整个程序。Future 内异常存入 Future，await 时重新抛出。
- 跨 `await` 边界：try 块含 await 时异常照常被外层 catch 捕获（编译器保证 unwind 路径）。

## 4. defer 与异常

- defer 在异常 unwind 时**先于**外层 catch 执行（函数帧销毁前）：

```ms
func work() {
    defer print("always")
    throw ValueError("boom")
}
try { work() } catch e { print(repr(e)) }
// 输出顺序：always → ValueError(...)
```

- defer 中抛出的异常按普通异常处理，且不阻止其余 defer 执行（逐个尽力执行）。

## 5. 惯用法

```ms
// 资源清理：defer 为主，try/finally 不再必要
var f = io.open("a.txt")
defer f.close()

// 错误构造：抛内置类型，消息带上下文
if x < 0 { throw ValueError("x must be >= 0, got ${x}") }

// 自定义异常
class AppError : Error {
    func __init__(msg, code) { this.msg = msg; this.code = code }
}
```

## 6. 与 Go error 风格的关系

mslang 只提供异常机制，不提供多返回值 error 惯例（多返回值本身支持，但不做 error 值约定）；需要"可忽略失败"的 API 返回 nil / Result 风格对象由标准库个别模块自行决定（如 `strconv` 风格 Parse 在 `strings` 模块提供 `tryInt(s) -> (v, ok)`）。
