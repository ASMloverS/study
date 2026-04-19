fun square(x) {
    return x * x
}
print(square(5))

fun fib(n) {
    if (n <= 1) { return n }
    return fib(n - 1) + fib(n - 2)
}
print(fib(10))
