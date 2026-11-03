// T018 05-functions.md §1-§3 示例端到端：声明仅加 print 包裹观察结果
// §2 的 list.map(xs, f) 全局形态当前无此内建（10 §3 仅方法形式），此处按 xs.map(f) 适配运行
func add(a, b) { return a + b }

func double(x) -> x * 2                 // 表达式体：单表达式函数简写

func greet(name, prefix = "Mr.") {      // 默认参数
    return "${prefix} ${name}"
}

func varargs(fmt, *args) {              // rest 参数收 list
    return [fmt, args]
}
print(add(1, 2))
print(double(21))
print(greet("Ada"))
print(varargs(1, 2, 3))

var f = func(x) { return x * 2 }
print(f(5))
var xs = [1, 2, 3]
print(xs.map(func(x) { return x + 1 }))

func counter() {
    var n = 0
    return func() { n += 1; return n }      // 捕获 n 的引用
}
var c = counter()
print(c())
print(c())
