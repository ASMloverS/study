# 04 语句

语句结尾不写分号（ASI，规则见 `01-lexical.md` §7）。块语句 `{ }` 必须显式（单语句也不例外，Go 风格）。

## 1. 声明语句

```ms
var x = 10                 // 显式声明，可无初值（默认 nil）
var y                       // y == nil
x := 20                    // 短声明：定义并赋值，仅函数/块作用域内合法
const MAX = 100            // 常量：首次赋值后不可再赋值（赋值抛 TypeError）
```

- `var`/`const` 可声明于任何作用域；`:=` 同。
- 同作用域重复声明（`var x` 后再 `x :=`）抛 SyntaxError；不同作用域遮蔽合法。
- `const` 仅约束"不可再绑定"，不深冻结容器（`const xs = []` 后 `xs.append(1)` 合法）。

## 2. 赋值语句

```ms
x = v                      // 普通赋值
xs[0] = v                  // 索引赋值（__setitem__）
obj.field = v              // 成员赋值
x += 1                     // 复合赋值：+= -= *= /= %= **= &= |= ^= <<= >>=
a, b = b, a                // 元组解构交换
a, b, *rest = xs           // 星号收集（rest 为 list）
for k, v in m.items() { }  // 解构在 for/in 中同样支持
```

- 解构目标数可少于右值（多余丢弃）；`*rest` 至多一个且可在任意位置（`a, *mid, z = xs`）；不可解构对象抛 TypeError。
- 复合赋值等价于读-算-写，不保证原子性（并发语义见 `08-concurrency.md` §8）。

## 3. if 语句

```ms
if x > 0 {
    ...
} else if x == 0 {
    ...
} else {
    ...
}
```

条件不加括号（写括号是 SyntaxError，强制 Go 风格统一）。

## 4. for / while

```ms
for i := 0; i < 10; i++ { ... }     // 三段式（init; cond; post，均可省略，cond 缺省为 true）
for x in xs { ... }                  // 迭代协议（__iter__/__next__）
for k, v in m.items() { ... }        // 解构迭代
for x in range(10) { ... }           // 与上同机制
for cond { ... }                     // 等价 while
for { ... }                          // 无限循环
while cond { ... }                   // Python 语义保留（脚本友好）
```

- `break` / `continue` 仅作用于最内层循环；无标签（v1 不做 label）。
- **循环变量每轮新建绑定**（两种形式均如此，闭包捕获安全）：`for i := 0; ...` 与 `for x in xs` 的头部分布变量在每轮迭代中是新鲜绑定，循环内闭包捕获拿到各自轮次的值（Go 1.22 / Python 3 语义）。
- `for x in chan`：持续接收直到 channel 关闭（见 `08-concurrency.md`）。

## 5. switch 语句

### 5.1 值 switch（无 fallthrough）

```ms
switch x {
case 1, 2:        // 多值命中
    ...
case 3:
    ...           // 隐式 break，不穿透
default:
    ...
}
```

- case 值可为任意表达式，`==` 语义匹配。
- 表达式形式：`switch f(x) { case ... }`。

### 5.2 类型 switch

```ms
switch type(v) {
case int, bigint:
    print("number")
case string as s:      // as 绑定：s 即 v（类型已确认，动态内核下等价改名）
    print(len(s))
case Point as p:
    p.move(1, 1)
default:
    print(type(v))
}
```

- 匹配使用 `isinstance` 语义；`as` 绑定新名进入 case 块作用域。

## 6. 控制转移

- `return expr?`：函数返回；无值返回 nil。顶层（模块体）return 非法。
- `break` / `continue`：仅循环体内合法。
- `throw expr`：抛出异常（见 `07-errors.md`）。

## 7. defer 语句

```ms
func copyFile(src, dst) {
    var f = io.open(src)
    defer f.close()
    return io.open(dst)
}
```

- defer 注册的调用在**函数退出时**按 LIFO 执行：正常 return、异常 unwind、协程被取消均触发。
- 注册时**立即求值参数**，延迟执行调用体。
- defer 语句只能在函数体内使用。

## 8. go 语句与 channel 语句

```ms
go worker(1, 2)          // 启动 goroutine
ch <- v                  // 发送（阻塞直至对端就绪或缓冲有位）
v = <-ch                 // 接收（表达式在 03，赋值在此）
<-ch                     // 丢弃接收
close(ch)                // 关闭
select {                 // 多路就绪选择
case v = <-ch1:
    ...
case ch2 <- x:
    ...
default:                 // 可选：无就绪时立即执行
    ...
}
```

完整语义（nil channel、关闭后行为、随机选择）见 `08-concurrency.md`。

## 9. import 语句

见 `09-modules.md`：`import "mod" as x`、`from "mod" import {a, b as c}`、`from "mod" import *`。

## 10. 表达式语句 / 块 / 空语句

- 任何表达式 + 换行即表达式语句。
- 块 `{ ... }` 开新作用域。
- 单独分号不允许出现在源码中（ASI 产物除外）。
