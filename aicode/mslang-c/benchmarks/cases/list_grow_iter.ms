// purpose : list grow + index iteration — stresses OP_BUILD_LIST and array access
// hot-op  : OP_SET_INDEX, OP_GET_INDEX
// expected: 4499998500000
var N = 3000000
var a = []
var i = 0
while (i < N) {
    a.push(i)
    i = i + 1
}
var sum = 0
var j = 0
while (j < N) {
    sum = sum + a[j]
    j = j + 1
}
print(sum)
