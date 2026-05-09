// purpose : recursive fibonacci — stresses OP_CALL / OP_RETURN
// hot-op  : OP_CALL, OP_RETURN
// expected: 9227465
var N = 35
fun fib(n) {
    if (n <= 1) { return n }
    return fib(n - 1) + fib(n - 2)
}
print(fib(N))
