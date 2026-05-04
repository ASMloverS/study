// purpose : hash map insert + lookup — stresses OP_SET_INDEX / OP_GET_INDEX on maps
// hot-op  : OP_SET_INDEX, OP_GET_INDEX
// expected: 4499998500000
var N = 3000000
var m = {}
var i = 0
while (i < N) {
    m[i] = i
    i = i + 1
}
var sum = 0
var j = 0
while (j < N) {
    sum = sum + m[j]
    j = j + 1
}
print(sum)
