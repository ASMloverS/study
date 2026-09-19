# 05 函数

## 1. 声明

```ms
func add(a, b) { return a + b }

func double(x) -> x * 2                 // 表达式体：单表达式函数简写

func greet(name, prefix = "Mr.") {      // 默认参数
    return "${prefix} ${name}"
}

func varargs(fmt, *args) {              // rest 参数收 list
    return [fmt, args]
}
```

- 参数表：普通参数 → 默认参数 → 至多一个 `*rest`。
- 调用侧：`f(1, *xs)` 星号展开（展开项排在位置实参之后，与默认参数按位对齐）。
- 无函数重载（后定义覆盖先定义）。
- 函数是一等公民：赋值、传参、入容器、闭包捕获均可。

## 2. 匿名函数与作用域

```ms
var f = func(x) { return x * 2 }
list.map(xs, func(x) { return x + 1 })
```

## 3. 闭包

- 内层函数捕获外层局部变量为**引用**（upvalue），非拷贝：

```ms
func counter() {
    var n = 0
    return func() { n += 1; return n }      // 捕获 n 的引用
}
var c = counter()
c()  // 1
c()  // 2
```

- 实现为 upvalue（开放/闭合链），见 `21-bytecode.md` CLOSURE 指令与 `22-object-model.md`。

## 4. 生成器函数

```ms
func fib() {
    var a, b = 0, 1
    for { yield a; a, b = b, a + b }
}
var g = fib()
next(g)    // 0
next(g)    // 1
for v in fib() { ... }         // 直接迭代
```

- `func*` 显式声明或函数体内出现 `yield` 即视为生成器（二选一时**推荐 func\***，避免误判）。
- 调用生成器函数不执行体，返回惰性 iterator；`next()` 推进到下一个 `yield`。
- 迭代协议：`iter(x)` / `next(it)`；`next` 耗尽返回 `nil`（不抛异常）；`for x in it` 遇 nil 停止。

## 5. 返回值

- `return a, b`：多返回值即返回 tuple，调用侧解构接收 `a, b = f()`。
- 无 return 或裸 `return` 返回 nil。

## 6. 函数内省

- `f.__name__`：函数名字符串。
- `f.__params__`：参数名列表（含默认参数名，不含 rest 的 `*`）。

## 7. 尾调用

不保证 TCO。深递归受栈上限保护（默认 1MB 协程栈，可调，超限抛 StackOverflowError）。

## 8. async 函数

`async func` 声明协程函数，调用返回 Future，体内可用 `await`——完整语义见 `08-concurrency.md`。async 函数与生成器共享协程机制但**不可混用**：async 函数内 `yield` 非法，生成器内 `await` 非法。

## 9. 原生函数（C 扩展）

C API 注册的原生函数在语言层与普通函数等价（`type()` 为 `function`）；见 `25-capi.md`。
