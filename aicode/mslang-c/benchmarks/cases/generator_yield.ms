// purpose : coroutine yield/resume — stresses the yield path
// hot-op  : OP_YIELD, OP_RESUME
// expected: 4499998500000
var N = 3000000
fun* range_gen(n) {
    var i = 0
    while (i < n) {
        yield i
        i = i + 1
    }
}
var gen = range_gen(N)
var sum = 0
var k = 0
while (k < N) {
    var v = resume(gen, nil)
    sum = sum + v
    k = k + 1
}
print(sum)
