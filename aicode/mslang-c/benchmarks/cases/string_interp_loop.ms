// purpose : string interpolation hot path — stresses template execution
// hot-op  : OP_CONCAT, OP_STR_INTERP
// expected: 29500000
var N = 5000000
var sum = 0
var i = 0
while (i < N) {
    var k = i % 100
    var s = "item${k}"
    sum = sum + len(s)
    i = i + 1
}
print(sum)
