// T019 生成器 fib（05 §4）：next 推进 + for-in 直迭代 + break 截断
func* fib() {
    var a = 0
    var b = 1
    for { yield a; a, b = b, a + b }
}
var g = fib()
print(next(g), next(g), next(g), next(g), next(g))
for v in fib() {
    if v > 50 { break }
    print(v)
}
