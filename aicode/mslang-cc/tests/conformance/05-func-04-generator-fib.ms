func* fib() {
    var a = 0
    var b = 1
    for {
        yield a
        a, b = b, a + b
    }
}
var g = fib()
