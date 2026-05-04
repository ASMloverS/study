// purpose : iterative fibonacci — stresses OP_LT_II / OP_ADD_II quickening
// hot-op  : OP_LT_II, OP_ADD_II, OP_LOOP
// expected: 12586269025
var N = 300000
// Run fib(50) iteratively, repeated N times to stress the loop
var result = 0
var outer = 0
while (outer < N) {
    var a = 0
    var b = 1
    var k = 2
    while (k <= 50) {
        var c = a + b
        a = b
        b = c
        k = k + 1
    }
    result = b
    outer = outer + 1
}
print(result)
