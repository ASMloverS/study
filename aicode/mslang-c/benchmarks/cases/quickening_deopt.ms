// purpose : deoptimization trigger — mix int/float args to force DEOPT_AND_RESPECIALIZE
// hot-op  : OP_ADD (deopt path), DEOPT_AND_RESPECIALIZE
// expected: 1.25e+13
var N = 5000000
fun add(a, b) { return a + b }
var s = 0
var i = 0
while (i < N) {
    if (i % 2 == 0) {
        s = s + add(i, 1)
    } else {
        s = s + int(add(i, 0.5))
    }
    i = i + 1
}
print(s)
