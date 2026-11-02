// T018 递归 fib（00-overview §3 前半 / 05 §1）：经全局名回查递归调用
func fib(n) {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}
print(fib(10))
print(fib(20))
